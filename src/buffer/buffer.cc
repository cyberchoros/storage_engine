#include <unistd.h>
#include <string.h>
#include <fcntl.h>

#include <iostream>

#include "buffer.h"

Buffer *get_buffer () {
    static Buffer buffer;
    return &buffer;
}

inline Buffer::Buffer () { dummy.last = dummy.next = &dummy; }

Buffer::~Buffer () {
    vector<string> paths;
    
    for (const auto& [path, file] : files) paths.push_back(path);
    for (string path : paths) quit_file(path);

    for (File *file : idles) delete file;
    for (Page *page : pages) delete page;
}

void Buffer::pull (Page *page) {
    page->last->next = page->next;
    page->next->last = page->last;
}

void Buffer::push (Page *page) {
    page->last = dummy.last;
    dummy.last->next = page;

    page->next = &dummy;
    dummy.last = page;
}

Page *Buffer::add_page (File *file, unsigned long page_id) {
    Page *page = get_page(file);

    page->init(file, page_id);

    if (page_id) push(page);
    else pinned[file->path] = page;

    return page;
}

Page *Buffer::get_page (File *file) {
    Page *page = NULL;

    if (pages.size() > 0) {
        page = pages.front();
        pages.erase(pages.begin());
    }
    else if (count < MEM_PAGE_NUM) {
        page = new Page();
        count += 1;
    }
    else page = lru_page(file);

    return page;
}

Page *Buffer::lru_page (File *file) {
    Page *page = dummy.next;

    if (page == &dummy) {
        page = pinned.begin()->second->file == file ? pinned.end()->second : pinned.begin()->second;
        quit_file(page->file->path);

        return get_page(file);
    }
    if (page->updated) page->write_back();

    page->file->pages.erase(page->page_id);
    pull(page);

    return page;
}

void Buffer::open_file (string path, bool flag) {
    File *file = NULL;

    if (idles.size() > 0) {
        file = idles.front();
        idles.erase(idles.begin());
    }
    else file = new File();

    file->path = path;

    if (flag) {
        file->file_id = open(path.c_str(), O_RDWR | O_CREAT, 0664);
        file->new_page();
    }
    else file->file_id = open(path.c_str(), O_RDWR, 0664);

    Page *page = add_page(file, 0);
    file->pages[0] = page;

    files[path] = file;
}

void Buffer::quit_file (string path) {
    File *file = files[path];

    for (const auto& [page_id, page] : file->pages) {
        if (page->updated) page->write_back();

        if (page_id) pull(page);
        else pinned.erase(path);

        pages.push_back(page);
    }
    close(file->file_id);
    file->pages.clear();

    files.erase(path);
    idles.push_back(file);
}

void Buffer::create_file (string path) { open_file(path, true); }

void Buffer::delete_file (string path) {
    if (files.find(path) != files.end()) quit_file(path);
    remove(path.c_str());
}

File *Buffer::operator[] (string path) {
    if (files.find(path) == files.end()) open_file(path);
    return files[path];
}

inline Info *File::fetch_info () { return (Info *)((*pages[0])[0]); }

Page *File::get_page (unsigned long page_id) {
    if (page_id == 0) return pages[0];
    
    Buffer *buffer = get_buffer();

    if (pages.find(page_id) == pages.end()) {
        Page *page = buffer->add_page(this, page_id);
        pages[page_id] = page;

        return page;
    }
    Page *page = pages[page_id];

    buffer->pull(page);
    buffer->push(page);

    return page;
}

void File::new_page () {
    Info info;
    info.head.page_id = info.head.offset = info.tail.page_id = 0;
    info.tail.offset = sizeof(Info);
    info.total = 1;

    char temp[PAGE_SIZE];
    memcpy(temp, &info, sizeof(Info));

    lseek(file_id, 0, SEEK_SET);
    write(file_id, temp, PAGE_SIZE);
}

void File::add_page () {
    Info *info = fetch_info();
    Page *page = get_buffer()->get_page(this);

    page->page_id = info->tail.page_id = info->total++;
    info->tail.offset = 0;

    page->file = this;
    page->write_back();

    pages[page->page_id] = page;
    get_buffer()->push(page);

    pages[0]->updated = true;
}

Addr File::insert_item (void *src, int size) {
    Info *info = fetch_info();
    Addr addr;

    if (info->head.page_id || info->head.offset)
        memcpy(&addr, &(info->head), sizeof(Addr));
    else if (PAGE_SIZE - info->tail.offset - sizeof(Addr) >= size)
        memcpy(&addr, &(info->tail), sizeof(Addr));
    else {
        add_page();
        return insert_item(src, size);
    }
    Page *page = get_page(addr.page_id);

    Addr *temp = (Addr *)((*page)[addr.offset]);

    if (info->head.page_id || info->head.offset) {
        info->head.page_id = temp->page_id;
        info->head.offset = temp->offset;
    }
    else info->tail.offset += sizeof(Addr) + size;

    pages[0]->updated = true;

    memcpy(temp, &addr, sizeof(Addr));
    memcpy(temp + 1, src, size);

    page->updated = true;

    return addr;
}

void File::remove_item (Addr addr) {
    Page *page = get_page(addr.page_id);
    Addr *temp = (Addr *)((*page)[addr.offset]);
    Info *info = fetch_info();

    temp->page_id = info->head.page_id;
    temp->offset = info->head.offset;

    page->updated = true;

    info->head.page_id = addr.page_id;
    info->head.offset = addr.offset;

    pages[0]->updated = true;
}

void File::update_item (Addr addr, void *src, int size) {
    Page *page = get_page(addr.page_id);

    Addr *temp = (Addr *)((*page)[addr.offset]) + 1;
    memcpy(temp, src, size);

    page->updated = true;
}

void File::search_item (Addr addr, void *tar, int size) {
    Addr *temp = (Addr *)((*get_page(addr.page_id))[addr.offset]) + 1;
    memcpy(tar, temp, size);
}

void Page::init (File *file, unsigned long page_id) {
    this->file = file;
    this->page_id = page_id;

    lseek(file->file_id, page_id * PAGE_SIZE, SEEK_SET);
    read(file->file_id, memory, PAGE_SIZE);
}

inline void *Page::operator[] (unsigned short offset) { return memory + offset; }

void Page::write_back () {
    lseek(file->file_id, page_id * PAGE_SIZE, SEEK_SET);
    write(file->file_id, memory, PAGE_SIZE);

    updated = false;
}
