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
// Checks if the list is empty
int list_empty(const struct list_head *head);

void list_add_tail(struct list_head *new, struct list_head *head);
// 回傳 member 在 type 裡的 byte offset
#define offsetof(type, member) ((unsigned long) &((type *)0)->member)
// 已知 ptr 指向某個 type 物件內部的 member 成員，回推 type 物件的起點位址。
#define list_entry(ptr, type, member) \
    ((type *)((char *)(ptr)-offsetof(type, member)))

// 在 iterate 中刪除元素時必用：先把 next 預存到 n，刪 pos 後 pos = n 才不會踩 dangling
#define list_for_each_entry_safe(pos, n, head, member) \
    for (pos = list_entry((head)->next, typeof(*pos), member), \
         n   = list_entry(pos->member.next, typeof(*pos), member); \
         &pos->member != (head); \
         pos = n, \
         n   = list_entry(n->member.next, typeof(*n), member))

#endif // __LIST_H__