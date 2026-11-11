#ifndef _SYS_QUEUE_H_
#define _SYS_QUEUE_H_

#define TAILQ_HEAD(name, type) \
struct name { \
    struct type *tqh_first; \
    struct type **tqh_last; \
}

#define TAILQ_HEAD_INITIALIZER(head) \
    { NULL, &(head).tqh_first }

#define TAILQ_ENTRY(type) \
struct { \
    struct type *tqe_next; \
    struct type **tqe_prev; \
}

#define TAILQ_INIT(head) do { \
    (head)->tqh_first = NULL; \
    (head)->tqh_last = &(head)->tqh_first; \
} while (0)

#define TAILQ_INSERT_TAIL(head, elm, field) do { \
    (elm)->field.tqe_next = NULL; \
    (elm)->field.tqe_prev = (head)->tqh_last; \
    *(head)->tqh_last = (elm); \
    (head)->tqh_last = &(elm)->field.tqe_next; \
} while (0)

#define TAILQ_REMOVE(head, elm, field) do { \
    if ((elm)->field.tqe_next != NULL) \
        (elm)->field.tqe_next->field.tqe_prev = (elm)->field.tqe_prev; \
    else \
        (head)->tqh_last = (elm)->field.tqe_prev; \
    *(elm)->field.tqe_prev = (elm)->field.tqe_next; \
} while (0)

#define TAILQ_FOREACH(var, head, field) \
    for ((var) = ((head)->tqh_first); (var); (var) = ((var)->field.tqe_next))

#define TAILQ_FOREACH_SAFE(var, head, field, tvar) \
    for ((var) = TAILQ_FIRST(head); \
        (var) && ((tvar) = TAILQ_NEXT(var, field), 1); \
        (var) = (tvar))

#define TAILQ_FIRST(head)       ((head)->tqh_first)
#define TAILQ_NEXT(elm, field)  ((elm)->field.tqe_next)
#define TAILQ_LAST(head, headname) \
    (*(((struct headname *)((head)->tqh_last))->tqh_last))
#define TAILQ_PREV(elm, headname, field) \
    (*(((struct headname *)((elm)->field.tqe_prev))->tqh_last))

#endif
