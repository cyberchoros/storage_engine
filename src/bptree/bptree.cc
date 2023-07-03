#include <unistd.h>
#include <string.h>

#include <iostream>

#include "bptree.h"

Bptree *get_bptree () {
    static Bptree bptree;
    return &bptree;
}

void Bptree::push (Node *node, Addr *addr, void *src, void *tar) {
    int cnt = addr - node->child;
    int num = node->total - cnt;

    memcpy(node->index + (cnt + 1) * VAL_SIZE, node->index + cnt * VAL_SIZE, num * VAL_SIZE);
    memcpy(node->index + cnt * VAL_SIZE, src, VAL_SIZE);

    memcpy(node->child + cnt + 1, node->child + cnt, num * sizeof(Addr));
    memcpy(node->child + cnt, tar, sizeof(Addr));
    
    node->total += 1;
}

void Bptree::pull (Node *node, Addr *addr) {
    int cnt = addr - node->child;
    int num = node->total - cnt - 1;

    memcpy(node->index + cnt * VAL_SIZE, node->index + (cnt + 1) * VAL_SIZE, num * VAL_SIZE);
    memcpy(node->child + cnt, node->child + cnt + 1, num * sizeof(Addr));
    
    node->total -= 1;
}

Attr Bptree::fetch_attr (string name) { return *(Attr *)(void *)((*get_buffer())[name_to_path(name) + ".idx"]->fetch_info()->reserved); }

int Bptree::create_form (string name, Attr *attr) {
    if (access((name_to_path(name) + ".idx").c_str(), F_OK) == 0) return INDEX_FILE_EXISTED;
    if (access((name_to_path(name) + ".db").c_str(), F_OK) == 0) return DB_FILE_EXISTED;

    Buffer *buffer = get_buffer();

    buffer->create_file(name_to_path(name) + ".idx");
    buffer->create_file(name_to_path(name) + ".db");

    File *file = (*buffer)[name_to_path(name) + ".idx"];

    Node root;

    root.leaf = true;
    root.total = 0;
    root.next.page_id = root.next.offset = 0;

    Addr addr = file->insert_item(&root, sizeof(Node));

    attr->head = attr->tail = addr;
    memcpy(file->fetch_info()->reserved, attr, sizeof(Attr));

    file->get_page(0)->updated = true;

    return 0;
}

int Bptree::delete_form (string name) {
    if (access((name_to_path(name) + ".idx").c_str(), F_OK)) return INDEX_FILE_NOT_FOUND;
    if (access((name_to_path(name) + ".db").c_str(), F_OK)) return DB_FILE_NOT_FOUND;

    Buffer *buffer = get_buffer();

    buffer->delete_file(name_to_path(name) + ".idx");
    buffer->delete_file(name_to_path(name) + ".db");

    return 0;
}

int Bptree::insert_data (string name, void *src, int (*cmp) (const void *, const void *, const int)) {
    if (access((name_to_path(name) + ".idx").c_str(), F_OK)) return INDEX_FILE_NOT_FOUND;
    if (access((name_to_path(name) + ".db").c_str(), F_OK)) return DB_FILE_NOT_FOUND;

    File *file = (*get_buffer())[name_to_path(name) + ".idx"];
    Attr *attr = (Attr *)(void *)(file->fetch_info()->reserved);
    Page *page = file->get_page(attr->head.page_id);
    Node *node = (Node *)(void *)((Addr *)(*page)[attr->head.offset] + 1);

    if (search_by_index(file, node, (char *)src + attr->index * VAL_SIZE, attr->val_size[attr->index], cmp)) return ITEM_EXISTED;

    Addr addr = (*get_buffer())[name_to_path(name) + ".db"]->insert_item(src, attr->count * VAL_SIZE);

    if (!insert_by_index(file, page, node, (char *)src + attr->index * VAL_SIZE, &addr, attr->val_size[attr->index], cmp)) return 0;

    return increment(file, attr, page, node);
}

int Bptree::increment (File *file, Attr *attr, Page *page, Node *node) {
    Node root;

    root.total = 1;
    memcpy(root.index, node->index, sizeof(VAL_SIZE));
    memcpy(root.child, &(attr->head), sizeof(Addr));

    split(file, &root, node, root.child);
    Addr addr = file->insert_item(&root, sizeof(Node));

    page->updated = true;
    attr->head = addr;
    file->get_page(0)->updated = true;

    return 0;
}

int Bptree::insert_by_index (File *file, Page *page, Node *root, void *src, void *tar, int size, int (*cmp) (const void *, const void *, const int)) {
    Addr *addr = binary_search(root, false, 0, root->total - 1, src, size, cmp);

    if (root->leaf) {
        addr ? push(root, addr + 1, src, tar) : push(root, root->child, src, tar);
        page->updated = true;

        return root->total == NODE_NUM ? 1 : 0;
    }
    addr = addr ? addr : root->child;

    Page *temp = file->get_page(addr->page_id);
    Node *node = (Node *)(void *)((Addr *)(*temp)[addr->offset] + 1);

    int tmp = insert_by_index(file, temp, node, src, tar, size, cmp);

    if (memcmp(root->index + (addr - root->child), node->index, VAL_SIZE)) {
        memcpy(root->index + (addr - root->child), node->index, VAL_SIZE);
        page->updated = true;
    }
    if (tmp) {
        split(file, root, node, addr);

        page->updated = true;
        temp->updated = true;
    }
    return root->total == NODE_NUM ? 1 : 0;
}

void Bptree::split (File *file, Node *root, Node *node, Addr *addr) {
    Node temp;

    temp.leaf = node->leaf;
    temp.total = node->total = node->total / 2;

    if (node->leaf) temp.next = node->next;

    memcpy(temp.index, node->index + VAL_SIZE * NODE_NUM / 2, VAL_SIZE * NODE_NUM / 2);
    memcpy(temp.child, node->child + NODE_NUM / 2, sizeof(Addr) * NODE_NUM / 2);

    Addr next = file->insert_item(&temp, sizeof(Node));

    if (node->leaf) node->next = next;

    push(root, addr + 1, temp.index, &next);
}

int Bptree::remove_data_by_index (string name, void *src, int (*cmp) (const void *, const void *, const int)) {
    if (access((name_to_path(name) + ".idx").c_str(), F_OK)) return INDEX_FILE_NOT_FOUND;
    if (access((name_to_path(name) + ".db").c_str(), F_OK)) return DB_FILE_NOT_FOUND;

    File *file = (*get_buffer())[name_to_path(name) + ".idx"];
    Attr *attr = (Attr *)(void *)(file->fetch_info()->reserved);
    Page *page = file->get_page(attr->head.page_id);
    Node *node = (Node *)(void *)((Addr *)(*page)[attr->head.offset] + 1);

    if (!search_by_index(file, node, src, attr->val_size[attr->index], cmp)) return ITEM_NOT_FOUND;

    remove_by_index(name, page, node, src, attr->val_size[attr->index], cmp);

    if (!node->leaf && node->total == 1) decrement(file, attr, node);

    return 0;
}

void Bptree::decrement (File *file, Attr *attr, Node *node) {
    Addr addr = node->child[0];
    file->remove_item(attr->head);

    attr->head = addr;
    file->get_page(0)->updated = true;
}

int Bptree::remove_by_index (string name, Page *page, Node *root, void *src, int size, int (*cmp) (const void *, const void *, const int)) {
    File *file = (*get_buffer())[name_to_path(name) + ".idx"];
    Addr *addr = binary_search(root, root->leaf, 0, root->total - 1, src, size, cmp);

    if (root->leaf) {
        (*get_buffer())[name_to_path(name) + ".db"]->remove_item(*addr);

        pull(root, addr);
        page->updated = true;

        return root->total < NODE_NUM / 2 ? 1 : 0; 
    }
    Page *temp = file->get_page(addr->page_id);
    Node *node = (Node *)(void *)((Addr *)(*temp)[addr->offset] + 1);

    int tmp = remove_by_index(name, temp, node, src, size, cmp);

    if (memcmp(root->index + (addr - root->child), node->index, VAL_SIZE)) {
        memcpy(root->index + (addr - root->child), node->index, VAL_SIZE);
        page->updated = true;
    }
    if (tmp) addr == root->child ? next_handle(file, page, temp, root, node, addr) : last_handle(file, page, temp, root, node, addr);

    return root->total < NODE_NUM / 2 ? 1 : 0;
}

void Bptree::last_handle (File *file, Page *page, Page *next, Node *root, Node *temp, Addr *addr) {
    Page *last = file->get_page((addr - 1)->page_id);
    Node *node = (Node *)(void *)((Addr *)(*last)[(addr - 1)->offset] + 1);

    if (node->total > NODE_NUM / 2) {
        push(temp, temp->child, node->index + (node->total - 1) * VAL_SIZE, node->child + node->total - 1);
        pull(node, node->child + node->total - 1);
        memcpy(root->index + (addr - root->child), temp->index, VAL_SIZE);

        page->updated = last->updated = next->updated = true;
    }
    else merge(file, page, last, root, node, temp, addr - 1);
}

void Bptree::next_handle (File *file, Page *page, Page *last, Node *root, Node *node, Addr *addr) {
    Page *next = file->get_page((addr + 1)->page_id);
    Node *temp = (Node *)(void *)((Addr *)(*next)[(addr + 1)->offset] + 1);

    if (temp->total > NODE_NUM / 2) {
        push(node, node->child + node->total, temp->index, temp->child);
        pull(temp, temp->child);
        memcpy(root->index + (addr - root->child) + 1, temp->index, VAL_SIZE);

        page->updated = last->updated = next->updated = true;
    }
    else merge(file, page, last, root, node, temp, addr);
}

void Bptree::merge (File *file, Page *page, Page *last, Node *root, Node *node, Node *temp, Addr *addr) {
    memcpy(node->index + node->total * VAL_SIZE, temp->index, temp->total * VAL_SIZE);
    memcpy(node->child + node->total, temp->child, temp->total * sizeof(Addr));

    node->total += temp->total;

    file->remove_item(*(addr + 1));
    pull(root, addr + 1);

    page->updated = last->updated = true;
}

int Bptree::update_data_by_index (string name, void *src, void *tar, int (*cmp) (const void *, const void *, const int)) {
    if (access((name_to_path(name) + ".idx").c_str(), F_OK)) return INDEX_FILE_NOT_FOUND;
    if (access((name_to_path(name) + ".db").c_str(), F_OK)) return DB_FILE_NOT_FOUND;

    File *file = (*get_buffer())[name_to_path(name) + ".idx"];
    Attr *attr = (Attr *)(void *)(file->fetch_info()->reserved);
    Node *node = (Node *)(void *)((Addr *)(*(file->get_page(attr->head.page_id)))[attr->head.offset] + 1);
    Addr *addr = search_by_index(file, node, src, attr->val_size[attr->index], cmp);

    if (addr) (*get_buffer())[name_to_path(name) + ".db"]->update_item(*addr, tar, attr->count * VAL_SIZE);

    return addr ? 0 : ITEM_NOT_FOUND;
}

int Bptree::search_data_by_index (string name, void *src, void *tar, int (*cmp) (const void *, const void *, const int)) {
    if (access((name_to_path(name) + ".idx").c_str(), F_OK)) return INDEX_FILE_NOT_FOUND;
    if (access((name_to_path(name) + ".db").c_str(), F_OK)) return DB_FILE_NOT_FOUND;

    File *file = (*get_buffer())[name_to_path(name) + ".idx"];
    Attr *attr = (Attr *)(void *)(file->fetch_info()->reserved);
    Node *node = (Node *)(void *)((Addr *)(*(file->get_page(attr->head.page_id)))[attr->head.offset] + 1);
    Addr *addr = search_by_index(file, node, src, attr->val_size[attr->index], cmp);

    if (addr) (*get_buffer())[name_to_path(name) + ".db"]->search_item(*addr, tar, attr->count * VAL_SIZE);

    return addr ? 0 : ITEM_NOT_FOUND;
}

Addr *Bptree::search_by_index (File *file, Node *node, void *src, int size, int (*cmp) (const void *, const void *, const int)) {
    Addr *addr = binary_search(node, node->leaf, 0, node->total - 1, src, size, cmp);

    if (node->leaf || !addr) return addr;
    Node *temp = (Node *)(void *)((Addr *)(*(file->get_page(addr->page_id)))[addr->offset] + 1);

    return search_by_index(file, temp, src, size, cmp);
}

Addr *Bptree::binary_search (Node *node, bool flag, int head, int tail, void *src, int size, int (*cmp) (const void *, const void *, const int)) {
    if (head > tail) return NULL;

    int temp = (head + tail) / 2;
    void *tar = node->index + temp * VAL_SIZE;

    if (cmp(src, tar, size) < 0)
        return binary_search(node, flag, head, temp - 1, src, size, cmp);
    if (flag ? cmp(src, tar, size) > 0 : temp < tail && cmp(src, (char *)tar + VAL_SIZE, size) >= 0)
        return binary_search(node, flag, temp + 1, tail, src, size, cmp);
    return node->child + temp;
}
