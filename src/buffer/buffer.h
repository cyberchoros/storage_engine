#ifndef _BUFFER_H_
#define _BUFFER_H_

#include <vector>
#include <unordered_map>
#include <string>

#include "../config.h"

using namespace std;

typedef struct {
    unsigned long page_id;
    unsigned short offset;
} Addr;

typedef struct {
    Addr head, tail;
    unsigned long total;
    char reserved[RESERVE_SPACE];
} Info;

class File;

class Page {
    friend class Bptree;
    friend class Buffer;
    friend class File;

    char memory[PAGE_SIZE];

    File *file;
    unsigned long page_id;
    bool updated;

    Page *last, *next;

    void init (File *file, unsigned long page_id);
    void *operator[] (unsigned short offset);
    void write_back ();
};

class File {
    friend class Bptree;
    friend class Buffer;
    friend class Page;

    unsigned int file_id;
    string path;

    unordered_map<unsigned long, Page*> pages;

    void add_page ();
    Page *get_page (unsigned long page_id);
    void new_page ();

public:
    Addr insert_item (void *src, int size);
    void remove_item (Addr addr);
    void update_item (Addr addr, void *src, int size);
    void search_item (Addr addr, void *tar, int size);
    Info *fetch_info ();
};

class Buffer {
    friend Buffer *get_buffer ();
    friend class File;

    unordered_map<string, File*> files;
    vector<File*> idles;

    Page dummy;

    vector<Page*> pages;
    unordered_map<string, Page*> pinned;

    unsigned long count;

    Buffer ();
    ~Buffer ();

    void pull (Page *page);
    void push (Page *page);

    Page *lru_page (File *file);
    Page *add_page (File *file, unsigned long page_id);
    Page *get_page (File *file);

    void open_file (string path, bool flag=false);
    void quit_file (string path);

public:
    void create_file (string path);
    void delete_file (string path);
    File *operator[] (string path);
};

Buffer *get_buffer ();

#endif
