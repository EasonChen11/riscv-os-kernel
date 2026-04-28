#ifndef __LIST_H__
#define __LIST_H__

struct list_head {
    struct list_head *next;
    struct list_head *prev;
};

// Initializes a list head to point to itself
void init_list_head(struct list_head *head);
// Adds a new entry between the specified head and the next entry
void list_add(struct list_head *new, struct list_head *head);
// Deletes an entry from the list
void list_del(struct list_head *entry);
// 摘下 entry 並把它變回自鏈狀態（safe：之後對它再 list_del 是 noop）
// list_del 之後若還可能對 entry 做 list_del 或 list_empty 檢查，用這個版本
void list_del_init(struct list_head *entry);
// Checks if the list is empty
int list_empty(const struct list_head *head);

void list_add_tail(struct list_head *new, struct list_head *head);
// 回傳 member 在 type 裡的 byte offset
#define offsetof(type, member) ((unsigned long) &((type *)0)->member)
// 已知 ptr 指向某個 type 物件內部的 member 成員，回推 type 物件的起點位址。
#define list_entry(ptr, type, member) \
    ((type *)((char *)(ptr)-offsetof(type, member)))

#endif // __LIST_H__