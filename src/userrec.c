/* userrec.c
 *
 * Copyright (C) 1997 Robey Pointer
 * Copyright (C) 1999 - 2006 Eggheads Development Team
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 * $Id: userrec.c,v 1.17 2006/11/25 13:14:32 tothwolf Exp $
 */

#include "main.h"
#include "users.h"
#include "chan.h"
#include "modules.h"

#include "userrec.h"
#include "botmsg.h"   /* simple_sprintf, botnet_send_* */
#include "dcc.h"      /* DCC_*, struct dcc_t */
#include "dccutil.h"  /* shareout, chanout_but */
#include "chanprog.h" /* findchan_by_dname */
#include "logfile.h"  /* putlog, LOG_* */
#include "match.h"    /* wild_match */
#include "mem.h"      /* n_malloc, nmalloc, n_realloc, nrealloc, nfree */
#include "misc.h"     /* strncpyz, str_escape, rmspace */
#include "rfc1459.h"  /* rfc_casecmp */
#include "userent.h"  /* list_type_expmem */

extern struct dcc_t *dcc;
extern struct chanset_t *chanset;
extern int default_flags, default_uflags, dcc_total, noxtra;
extern time_t now;

int noshare = 1;                   /* don't send out to sharebots   */
struct userrec *userlist = NULL;   /* user records are stored here  */
struct userrec *lastuser = NULL;   /* last accessed user record     */
maskrec *global_bans = NULL, *global_exempts = NULL, *global_invites = NULL;
struct igrec *global_ign = NULL;
int cache_hit = 0, cache_miss = 0; /* temporary cache accounting    */
int strict_ident = 1;



void set_chanlist(const char *host, struct userrec *rec)
{
  char *nick, *uhost, buf[UHOSTLEN];
  register memberlist *m;
  register struct chanset_t *chan;

  strncpyz(buf, host, sizeof buf);
  uhost = buf;
  nick = splitnick(&uhost);
  for (chan = chanset; chan; chan = chan->next) {
    for (m = chan->channel.member; m && m->nick[0]; m = m->next) {
      if (!rfc_casecmp(nick, m->nick) && !egg_strcasecmp(uhost, m->userhost))
        m->user = rec;
    }
  }
}

/* Clear the user pointers in the chanlists.
 *
 * Necessary when a hostmask is added/removed, a user is added or a new
 * userfile is loaded.
 */
void clear_chanlist(void)
{
  register memberlist *m;
  register struct chanset_t *chan;

  for (chan = chanset; chan; chan = chan->next) {
    for (m = chan->channel.member; m && m->nick[0]; m = m->next) {
      m->user = NULL;
      m->tried_getuser = 0;
    }
  }
}

/* Shortcut for get_user_by_host -- might have user record in one of the
 * channel caches.
 */
struct userrec *check_chanlist(const char *host)
{
  char *nick, *uhost, buf[UHOSTLEN];
  register memberlist *m;
  register struct chanset_t *chan;

  strncpyz(buf, host, sizeof buf);
  uhost = buf;
  nick = splitnick(&uhost);
  for (chan = chanset; chan; chan = chan->next) {
    for (m = chan->channel.member; m && m->nick[0]; m = m->next) {
      if (!rfc_casecmp(nick, m->nick) && !egg_strcasecmp(uhost, m->userhost))
        return m->user;
    }
  }
  return NULL;
}

/* Shortcut for get_user_by_handle --  might have user record in one of the
 * channel caches.
 */
struct userrec *check_chanlist_hand(const char *hand)
{
  register struct chanset_t *chan;
  register memberlist *m;

  for (chan = chanset; chan; chan = chan->next) {
    for (m = chan->channel.member; m && m->nick[0]; m = m->next) {
      if (m->user && !egg_strcasecmp(m->user->handle, hand))
        return m->user;
    }
  }
  return NULL;
}

/* Clear the user pointer of a specific nick in the chanlists.
 *
 * Necessary when a hostmask is added/removed, a nick changes, etc.
 * Does not completely invalidate the channel cache like clear_chanlist().
 */
void clear_chanlist_member(const char *nick)
{
  register memberlist *m;
  register struct chanset_t *chan;

  for (chan = chanset; chan; chan = chan->next) {
    for (m = chan->channel.member; m && m->nick[0]; m = m->next) {
      if (!rfc_casecmp(m->nick, nick)) {
        m->user = NULL;
        m->tried_getuser = 0;
        break;
      }
    }
  }
}

void *_user_malloc(int size, const char *file, int line)
{
#ifdef DEBUG_MEM
  char x[1024];
  const char *p;

  p = strrchr(file, '/');
  simple_sprintf(x, "userrec.c:%s", p ? p + 1 : file);
  return n_malloc(size, x, line);
#else
  return nmalloc(size);
#endif
}

void *_user_realloc(void *ptr, int size, const char *file, int line)
{
#ifdef DEBUG_MEM
  char x[1024];
  const char *p;

  p = strrchr(file, '/');
  simple_sprintf(x, "userrec.c:%s", p ? p + 1 : file);
  return n_realloc(ptr, size, x, line);
#else
  return nrealloc(ptr, size);
#endif
}

inline int expmem_mask(struct maskrec *m)
{
  int result = 0;

  for (; m; m = m->next) {
    result += sizeof(struct maskrec);
    result += strlen(m->mask) + 1;
    if (m->user)
      result += strlen(m->user) + 1;
    if (m->desc)
      result += strlen(m->desc) + 1;
  }

  return result;
}

/* Memory we should be using
 */
int expmem_users()
{
  int tot;
  struct userrec *u;
  struct chanuserrec *ch;
  struct chanset_t *chan;
  struct user_entry *ue;
  struct igrec *i;

  tot = 0;
  for (u = userlist; u; u = u->next) {
    for (ch = u->chanrec; ch; ch = ch->next) {
      tot += sizeof(struct chanuserrec);

      if (ch->info != NULL)
        tot += strlen(ch->info) + 1;
    }
    tot += sizeof(struct userrec);

    for (ue = u->entries; ue; ue = ue->next) {
      tot += sizeof(struct user_entry);

      if (ue->name) {
        tot += strlen(ue->name) + 1;
        tot += list_type_expmem(ue->u.list);
      } else
        tot += ue->type->expmem(ue);
    }
  }
  /* Account for each channel's masks */
  for (chan = chanset; chan; chan = chan->next) {

    /* Account for each channel's ban-list user */
    tot += expmem_mask(chan->bans);

    /* Account for each channel's exempt-list user */
    tot += expmem_mask(chan->exempts);

    /* Account for each channel's invite-list user */
    tot += expmem_mask(chan->invites);
  }

  tot += expmem_mask(global_bans);
  tot += expmem_mask(global_exempts);
  tot += expmem_mask(global_invites);

  for (i = global_ign; i; i = i->next) {
    tot += sizeof(struct igrec);

    tot += strlen(i->igmask) + 1;
    if (i->user)
      tot += strlen(i->user) + 1;
    if (i->msg)
      tot += strlen(i->msg) + 1;
  }
  return tot;
}

int count_users(struct userrec *bu)
{
  int tot = 0;
  struct userrec *u;

  for (u = bu; u; u = u->next)
    tot++;
  return tot;
}

/* Removes a username prefix '+' from a userhost.
 * and changes prefixes '-', '^', '=' into '~'
 * e.g, "nick!+user@host" -> "nick!user@host"
 *      "nick!^user@host" -> "nick!~user@host"
 */
char *fixfrom(char *s)
{
  char *p = NULL;

  if (!s || !*s || strict_ident)
    return s;

  if ((p = strchr(s, '!'))) {
    if (!*(++p))
      return s;	/* There's nothing following "!". */
  } else
    p = s; /* There's no nick. */

  if (*(p + 1) == '@') /* is that checking needed? (takeda) */
    return s;

  if (*p == '+')
    memmove(p, p + 1, strlen(p)); /* NUL is included without +1. */
  else if (strchr("-^=", *p))
    *p = '~';

  return s;
}

struct userrec *check_dcclist_hand(char *handle)
{
  int i;

  for (i = 0; i < dcc_total; i++)
    if (!egg_strcasecmp(dcc[i].nick, handle))
      return dcc[i].user;
  return NULL;
}

struct userrec *get_user_by_handle(struct userrec *bu, char *handle)
{
  struct userrec *u, *ret;

  if (!handle)
    return NULL;
  /* FIXME: This should be done outside of this function. */
  rmspace(handle);
  if (!handle[0] || (handle[0] == '*'))
    return NULL;
  if (bu == userlist) {
    if (lastuser && !egg_strcasecmp(lastuser->handle, handle)) {
      cache_hit++;
      return lastuser;
    }
    ret = check_dcclist_hand(handle);
    if (ret) {
      cache_hit++;
      return ret;
    }
    ret = check_chanlist_hand(handle);
    if (ret) {
      cache_hit++;
      return ret;
    }
    cache_miss++;
  }
  for (u = bu; u; u = u->next)
    if (!egg_strcasecmp(u->handle, handle)) {
      if (bu == userlist)
        lastuser = u;
      return u;
    }
  return NULL;
}

/* Fix capitalization, etc
 */
void correct_handle(char *handle)
{
  struct userrec *u;

  u = get_user_by_handle(userlist, handle);
  if (u == NULL || handle == u->handle)
    return;
  strcpy(handle, u->handle);
}

/* This will be usefull in a lot of places, much more code re-use so we
 * endup with a smaller executable bot. <cybah>
 */
void clear_masks(maskrec *m)
{
  maskrec *temp = NULL;

  for (; m; m = temp) {
    temp = m->next;
    if (m->mask)
      nfree(m->mask);
    if (m->user)
      nfree(m->user);
    if (m->desc)
      nfree(m->desc);
    nfree(m);
  }
}

void clear_userlist(struct userrec *bu)
{
  struct userrec *u, *v;
  int i;

  for (u = bu; u; u = v) {
    v = u->next;
    freeuser(u);
  }
  if (userlist == bu) {
    struct chanset_t *cst;

    for (i = 0; i < dcc_total; i++)
      dcc[i].user = NULL;
    clear_chanlist();
    lastuser = NULL;

    while (global_ign)
      delignore(global_ign->igmask);

    clear_masks(global_bans);
    clear_masks(global_exempts);
    clear_masks(global_invites);
    global_exempts = global_invites = global_bans = NULL;

    for (cst = chanset; cst; cst = cst->next) {
      clear_masks(cst->bans);
      clear_masks(cst->exempts);
      clear_masks(cst->invites);

      cst->bans = cst->exempts = cst->invites = NULL;
    }
  }
  /* Remember to set your userlist to NULL after calling this */
}

/* Find CLOSEST host match
 * (if "*!*@*" and "*!*@*clemson.edu" both match, use the latter!)
 *
 * Checks the chanlist first, to possibly avoid needless search.
 */
struct userrec *get_user_by_host(char *host)
{
  struct userrec *u, *ret;
  struct list_type *q;
  int cnt, i;
  char host2[UHOSTLEN];

  if (host == NULL)
    return NULL;
  rmspace(host);
  if (!host[0])
    return NULL;
  ret = check_chanlist(host);
  cnt = 0;
  if (ret != NULL) {
    cache_hit++;
    return ret;
  }
  cache_miss++;
  strncpyz(host2, host, sizeof host2);
  fixfrom(host);
  for (u = userlist; u; u = u->next) {
    q = get_user(&USERENTRY_HOSTS, u);
    for (; q; q = q->next) {
      i = wild_match(q->extra, host);
      if (i > cnt) {
        ret = u;
        cnt = i;
      }
    }
  }
  if (ret != NULL) {
    lastuser = ret;
    set_chanlist(host2, ret);
  }
  return ret;
}

/* use fixfrom() or dont? (drummer)
 *
 * looks like that function is used nowhere in eggdrop (takeda)
 */
struct userrec *get_user_by_equal_host(char *host)
{
  struct userrec *u;
  struct list_type *q;

  for (u = userlist; u; u = u->next)
    for (q = get_user(&USERENTRY_HOSTS, u); q; q = q->next)
      if (!rfc_casecmp(q->extra, host))
        return u;
  return NULL;
}

/* Try: pass_match_by_host("-",host)
 * will return 1 if no password is set for that host
 */
int u_pass_match(struct userrec *u, char *pass)
{
  char *cmp, new[32];

  if (!u)
    return 0;
  cmp = get_user(&USERENTRY_PASS, u);
  if (!cmp && (!pass[0] || (pass[0] == '-')))
    return 1;
  if (!cmp || !pass || !pass[0] || (pass[0] == '-'))
    return 0;
  if (u->flags & USER_BOT) {
    if (!strcmp(cmp, pass))
      return 1;
  } else {
    if (strlen(pass) > 30)
      pass[30] = 0;
    encrypt_pass(pass, new);
    if (!strcmp(cmp, new))
      return 1;
  }
  return 0;
}

int change_handle(struct userrec *u, char *newh)
{
  int i;
  char s[HANDLEN + 1];

  if (!u)
    return 0;
  /* Nothing that will confuse the userfile */
  if (!newh[1] && strchr(BADHANDCHARS, newh[0]))
    return 0;
  check_tcl_nkch(u->handle, newh);
  /* Yes, even send bot nick changes now: */
  if (!noshare && !(u->flags & USER_UNSHARED))
    shareout(NULL, "h %s %s\n", u->handle, newh);
  strncpyz(s, u->handle, sizeof s);
  strncpyz(u->handle, newh, sizeof u->handle);
  for (i = 0; i < dcc_total; i++)
    if (dcc[i].type != &DCC_BOT && !egg_strcasecmp(dcc[i].nick, s)) {
      strncpyz(dcc[i].nick, newh, sizeof dcc[i].nick);
      if (dcc[i].type == &DCC_CHAT && dcc[i].u.chat->channel >= 0) {
        chanout_but(-1, dcc[i].u.chat->channel,
                    "*** Handle change: %s -> %s\n", s, newh);
        if (dcc[i].u.chat->channel < GLOBAL_CHANS)
          botnet_send_nkch(i, s);
      }
    }
  return 1;
}

struct userrec *adduser(struct userrec *bu, char *handle, char *host,
                        char *pass, int flags)
{
  struct userrec *u, *x;
  struct xtra_key *xk;
  int oldshare = noshare;
  long tv;

  noshare = 1;
  u = nmalloc(sizeof *u);

  /* u->next=bu; bu=u; */
  strncpyz(u->handle, handle, sizeof u->handle);
  u->next = NULL;
  u->chanrec = NULL;
  u->entries = NULL;
  if (flags != USER_DEFAULT) {  /* drummer */
    u->flags = flags;
    u->flags_udef = 0;
  } else {
    u->flags = default_flags;
    u->flags_udef = default_uflags;
  }
  set_user(&USERENTRY_PASS, u, pass);
  if (!noxtra) {
    char *now2;
    xk = nmalloc(sizeof *xk);
    xk->key = nmalloc(8);
    strcpy(xk->key, "created");
    now2 = nmalloc(15);
    tv = now;
    sprintf(now2, "%li", tv);
    xk->data = nmalloc(strlen(now2) + 1);
    sprintf(xk->data, "%li", tv);
    set_user(&USERENTRY_XTRA, u, xk);
    nfree(now2);
  }
  /* Strip out commas -- they're illegal */
  if (host && host[0]) {
    char *p;

    /* About this fixfrom():
     *   We should use this fixfrom before every call of adduser()
     *   but its much easier to use here...  (drummer)
     *   Only use it if we have a host :) (dw)
     */
    fixfrom(host);

    p = strchr(host, ',');
    while (p != NULL) {
      *p = '?';
      p = strchr(host, ',');
    }
    set_user(&USERENTRY_HOSTS, u, host);
  } else
    set_user(&USERENTRY_HOSTS, u, "none");
  if (bu == userlist)
    clear_chanlist();
  noshare = oldshare;
  if (!noshare && handle[0] != '*' && !(flags & USER_UNSHARED) &&
      bu == userlist) {
    struct flag_record fr = { FR_GLOBAL, 0, 0, 0, 0, 0 };
    char x[100];

    fr.global = u->flags;

    fr.udef_global = u->flags_udef;
    build_flags(x, &fr, 0);
    shareout(NULL, "n %s %s %s %s\n", handle, host && host[0] ? host : "none",
             pass, x);
  }
  if (bu == NULL)
    bu = u;
  else {
    if (bu == userlist && lastuser != NULL)
      x = lastuser;
    else
      x = bu;
    while (x->next != NULL)
      x = x->next;
    x->next = u;
    if (bu == userlist)
      lastuser = u;
  }

  return bu;
}

void freeuser(struct userrec *u)
{
  struct user_entry *ue, *ut;
  struct chanuserrec *ch, *z;

  if (u == NULL)
    return;

  ch = u->chanrec;
  while (ch) {
    z = ch;
    ch = ch->next;
    if (z->info != NULL)
      nfree(z->info);
    nfree(z);
  }
  u->chanrec = NULL;
  for (ue = u->entries; ue; ue = ut) {
    ut = ue->next;
    if (ue->name) {
      struct list_type *lt, *ltt;

      for (lt = ue->u.list; lt; lt = ltt) {
        ltt = lt->next;
        nfree(lt->extra);
        nfree(lt);
      }
      nfree(ue->name);
      nfree(ue);
    } else
      ue->type->kill(ue);
  }
  nfree(u);
}

int deluser(char *handle)
{
  struct userrec *u = userlist, *prev = NULL;
  int fnd = 0;

  while ((u != NULL) && !fnd) {
    if (!egg_strcasecmp(u->handle, handle))
      fnd = 1;
    else {
      prev = u;
      u = u->next;
    }
  }
  if (!fnd)
    return 0;
  if (prev == NULL)
    userlist = u->next;
  else
    prev->next = u->next;
  if (!noshare && (handle[0] != '*') && !(u->flags & USER_UNSHARED))
    shareout(NULL, "k %s\n", handle);
  for (fnd = 0; fnd < dcc_total; fnd++)
    if (dcc[fnd].user == u)
      dcc[fnd].user = 0;        /* Clear any dcc users for this entry,
                                 * null is safe-ish */
  clear_chanlist();
  freeuser(u);
  lastuser = NULL;
  return 1;
}

int delhost_by_handle(char *handle, char *host)
{
  struct userrec *u;
  struct list_type *q, *qnext, *qprev;
  struct user_entry *e = NULL;
  int i = 0;

  u = get_user_by_handle(userlist, handle);
  if (!u)
    return 0;
  q = get_user(&USERENTRY_HOSTS, u);
  qprev = q;
  if (q) {
    if (!rfc_casecmp(q->extra, host)) {
      e = find_user_entry(&USERENTRY_HOSTS, u);
      e->u.extra = q->next;
      nfree(q->extra);
      nfree(q);
      i++;
      qprev = NULL;
      q = e->u.extra;
    } else
      q = q->next;
    while (q) {
      qnext = q->next;
      if (!rfc_casecmp(q->extra, host)) {
        if (qprev)
          qprev->next = q->next;
        else if (e) {
          e->u.extra = q->next;
          qprev = NULL;
        }
        nfree(q->extra);
        nfree(q);
        i++;
      } else
        qprev = q;
      q = qnext;
    }
  }
  if (!qprev)
    set_user(&USERENTRY_HOSTS, u, "none");
  if (!noshare && i && !(u->flags & USER_UNSHARED))
    shareout(NULL, "-h %s %s\n", handle, host);
  clear_chanlist();
  return i;
}

void addhost_by_handle(char *handle, char *host)
{
  struct userrec *u = get_user_by_handle(userlist, handle);

  set_user(&USERENTRY_HOSTS, u, host);
  /* u will be cached, so really no overhead, even tho this looks dumb: */
  if (!noshare && !(u->flags & USER_UNSHARED)) {
    if (u->flags & USER_BOT)
      shareout(NULL, "+bh %s %s\n", handle, host);
    else
      shareout(NULL, "+h %s %s\n", handle, host);
  }
  clear_chanlist();
}

void touch_laston(struct userrec *u, char *where, time_t timeval)
{
  if (!u)
    return;

  if (timeval > 1) {
    struct laston_info *li = get_user(&USERENTRY_LASTON, u);

    if (!li)
      li = nmalloc(sizeof *li);

    else if (li->lastonplace)
      nfree(li->lastonplace);
    li->laston = timeval;
    if (where) {
      li->lastonplace = nmalloc(strlen(where) + 1);
      strcpy(li->lastonplace, where);
    } else
      li->lastonplace = NULL;
    set_user(&USERENTRY_LASTON, u, li);
  } else if (timeval == 1)
    set_user(&USERENTRY_LASTON, u, 0);
}

/*  Go through all channel records and try to find a matching
 *  nick. Will return the user's user record if that is known
 *  to the bot.  (Fabian)
 *
 *  Warning: This is unreliable by concept!
 */
struct userrec *get_user_by_nick(char *nick)
{
  struct chanset_t *chan;
  memberlist *m;

  for (chan = chanset; chan; chan = chan->next) {
    for (m = chan->channel.member; m && m->nick[0]; m = m->next) {
      if (!rfc_casecmp(nick, m->nick)) {
        char word[512];

        egg_snprintf(word, sizeof word, "%s!%s", m->nick, m->userhost);
        /* No need to check the return value ourself */
        return get_user_by_host(word);
      }
    }
  }
  /* Sorry, no matches */
  return NULL;
}

void user_del_chan(char *dname)
{
  struct chanuserrec *ch, *och;
  struct userrec *u;

  for (u = userlist; u; u = u->next) {
    ch = u->chanrec;
    och = NULL;
    while (ch) {
      if (!rfc_casecmp(dname, ch->channel)) {
        if (och)
          och->next = ch->next;
        else
          u->chanrec = ch->next;

        if (ch->info)
          nfree(ch->info);
        nfree(ch);
        break;
      }
      och = ch;
      ch = ch->next;
    }
  }
}
