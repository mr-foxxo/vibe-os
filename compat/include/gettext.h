#ifndef _GETTEXT_H
#define _GETTEXT_H

#define gettext(msgid) (msgid)
#define dgettext(domainname, msgid) (msgid)
#define dcgettext(domainname, msgid, category) (msgid)
#define ngettext(msgid, msgid_plural, n) ((n) == 1 ? (msgid) : (msgid_plural))
#define dngettext(domainname, msgid, msgid_plural, n) ((n) == 1 ? (msgid) : (msgid_plural))
#define dcngettext(domainname, msgid, msgid_plural, n, category) ((n) == 1 ? (msgid) : (msgid_plural))

#endif /* _GETTEXT_H */
