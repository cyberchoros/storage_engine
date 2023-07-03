#ifndef _BPTREE_H_
#define _BPTREE_H_

#include <string>

#include "../buffer/buffer.h"
#include "../config.h"
#include "../error.h"

using namespace std;

typedef struct {
    char index, count;
    char name[ITEM_NUM * KEY_SIZE];
    char key_size[ITEM_NUM];
    char val_size[ITEM_NUM];
    char type[ITEM_NUM];
    Addr head, tail;
} Attr;

typedef struct {
    bool leaf;
    char total;
    char index[NODE_NUM * VAL_SIZE];
    Addr child[NODE_NUM];
    Addr last, next;
} Node;

class Bptree {
    friend Bptree *get_bptree ();

    void push (Node *node, Addr *addr, void *src, void *tar);
    void pull (Node *node, Addr *addr);

    int increment (File *file, Attr *attr, Page *page, Node *node);
    int insert_by_index (File *file, Page *page, Node *root, void *src, void *tar, int size, int (*cmp) (const void *, const void *, const int));
    void split (File *file, Node *root, Node *node, Addr *addr);

    void decrement (File *file, Attr *attr, Node *node);
    int remove_by_index (string name, Page *page, Node *root, void *src, int size, int (*cmp) (const void *, const void *, const int));
    void last_handle (File *file, Page *page, Page *next, Node *root, Node *temp, Addr *addr);
    void next_handle (File *file, Page *page, Page *last, Node *root, Node *node, Addr *addr);
    void merge (File *file, Page *page, Page *last, Node *root, Node *node, Node *temp, Addr *addr);

    Addr *search_by_index (File *file, Node *node, void *src, int size, int (*cmp) (const void *, const void *, const int));
    Addr *binary_search (Node *node, bool flag, int head, int tail, void *src, int size, int (*cmp) (const void *, const void *, const int));

public:
    int create_form (string name, Attr *attr);
    int delete_form (string name);

    int insert_data (string name, void *src, int (*cmp) (const void *, const void *, const int));
    int remove_data_by_index (string name, void *src, int (*cmp) (const void *, const void *, const int));
    int update_data_by_index (string name, void *src, void *tar, int (*cmp) (const void *, const void *, const int));
    int search_data_by_index (string name, void *src, void *tar, int (*cmp) (const void *, const void *, const int));

    Attr fetch_attr (string name);
};

Bptree *get_bptree ();

#endif
