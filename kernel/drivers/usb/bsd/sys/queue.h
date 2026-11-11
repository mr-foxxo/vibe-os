#ifndef _SYS_QUEUE_H_
#define _SYS_QUEUE_H_

#include <sys/_null.h>

#define SIMPLEQ_HEAD(name, type) \
struct name { struct type *sqh_first; struct type **sqh_last; }
#define SIMPLEQ_ENTRY(type) \
struct { struct type *sqe_next; }
#define SIMPLEQ_INIT(head) do { (head)->sqh_first = NULL; (head)->sqh_last = &(head)->sqh_first; } while(0)
#define SIMPLEQ_INSERT_HEAD(head, elm, field) do { \
    (elm)->field.sqe_next = (head)->sqh_first; \
    if ((head)->sqh_first == NULL) (head)->sqh_last = &(elm)->field.sqe_next; \
    (head)->sqh_first = (elm); } while(0)
#define SIMPLEQ_INSERT_TAIL(head, elm, field) do { \
    (elm)->field.sqe_next = NULL; \
    *(head)->sqh_last = (elm); \
    (head)->sqh_last = &(elm)->field.sqe_next; } while(0)
#define SIMPLEQ_REMOVE_HEAD(head, field) do { \
    (head)->sqh_first = (head)->sqh_first->field.sqe_next; \
    if ((head)->sqh_first == NULL) (head)->sqh_last = &(head)->sqh_first; } while(0)
#define SIMPLEQ_FIRST(head) ((head)->sqh_first)
#define SIMPLEQ_NEXT(elm, field) ((elm)->field.sqe_next)
#define SIMPLEQ_EMPTY(head) ((head)->sqh_first == NULL)

#define LIST_HEAD(name, type) \
struct name { struct type *lh_first; }
#define LIST_ENTRY(type) \
struct { struct type *le_next; struct type **le_prev; }
#define LIST_INIT(head) do { (head)->lh_first = NULL; } while(0)
#define LIST_INSERT_HEAD(head, elm, field) do { \
    if (((elm)->field.le_next = (head)->lh_first) != NULL) \
        (head)->lh_first->field.le_prev = &(elm)->field.le_next; \
    (head)->lh_first = (elm); \
    (elm)->field.le_prev = &(head)->lh_first; } while(0)
#define LIST_REMOVE(elm, field) do { \
    if ((elm)->field.le_next != NULL) \
        (elm)->field.le_next->field.le_prev = (elm)->field.le_prev; \
    *(elm)->field.le_prev = (elm)->field.le_next; } while(0)
#define LIST_FIRST(head) ((head)->lh_first)
#define LIST_NEXT(elm, field) ((elm)->field.le_next)
#define LIST_EMPTY(head) ((head)->lh_first == NULL)
#define LIST_INSERT_AFTER(listelm, elm, field) do { \
    if (((elm)->field.le_next = (listelm)->field.le_next) != NULL) \
        (listelm)->field.le_next->field.le_prev = &(elm)->field.le_next; \
    (listelm)->field.le_next = (elm); \
    (elm)->field.le_prev = &(listelm)->field.le_next; } while(0)

#define TAILQ_HEAD(name, type) \
struct name { struct type *tqh_first; struct type **tqh_last; }
#define TAILQ_ENTRY(type) \
struct { struct type *tqe_next; struct type **tqe_prev; }
#define TAILQ_INIT(head) do { (head)->tqh_first = NULL; (head)->tqh_last = &(head)->tqh_first; } while(0)
#define TAILQ_INSERT_TAIL(head, elm, field) do { \
    (elm)->field.tqe_next = NULL; \
    (elm)->field.tqe_prev = (head)->tqh_last; \
    *(head)->tqh_last = (elm); \
    (head)->tqh_last = &(elm)->field.tqe_next; } while(0)
#define TAILQ_REMOVE(head, elm, field) do { \
    if (((elm)->field.tqe_next) != NULL) \
        (elm)->field.tqe_next->field.tqe_prev = (elm)->field.tqe_prev; \
    else \
        (head)->tqh_last = (elm)->field.tqe_prev; \
    *(elm)->field.tqe_prev = (elm)->field.tqe_next; } while(0)
#define TAILQ_FIRST(head) ((head)->tqh_first)
#define TAILQ_NEXT(elm, field) ((elm)->field.tqe_next)
#define TAILQ_LAST(head, headname) (*((head)->tqh_last))
#define TAILQ_EMPTY(head) ((head)->tqh_first == NULL)
#define TAILQ_FOREACH(var, head, field) for ((var) = TAILQ_FIRST(head); (var); (var) = TAILQ_NEXT(var, field))
#define TAILQ_FOREACH_SAFE(var, head, field, tvar) \
    for ((var) = TAILQ_FIRST(head); (var) && ((tvar) = TAILQ_NEXT(var, field), 1); (var) = (tvar))
#define TAILQ_INSERT_HEAD(head, elm, field) do { \
    if (((elm)->field.tqe_next = (head)->tqh_first) != NULL) \
        (head)->tqh_first->field.tqe_prev = &(elm)->field.tqe_next; \
    else \
        (head)->tqh_last = &(elm)->field.tqe_next; \
    (head)->tqh_first = (elm); \
    (elm)->field.tqe_prev = &(head)->tqh_first; } while(0)

#define LIST_HEAD_INITIALIZER(head) { NULL }

#define SLIST_HEAD(name, type) \
struct name { struct type *slh_first; }
#define SLIST_ENTRY(type) \
struct { struct type *sle_next; }
#define SLIST_INIT(head) do { (head)->slh_first = NULL; } while(0)
#define SLIST_INSERT_HEAD(head, elm, field) do { \
    (elm)->field.sle_next = (head)->slh_first; \
    (head)->slh_first = (elm); } while(0)
#define SLIST_FIRST(head) ((head)->slh_first)
#define SLIST_NEXT(elm, field) ((elm)->field.sle_next)
#define SLIST_REMOVE_HEAD(head, field) do { \
    (head)->slh_first = (head)->slh_first->field.sle_next; } while(0)
#define SLIST_EMPTY(head) ((head)->slh_first == NULL)

#endif
