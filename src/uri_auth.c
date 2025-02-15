/*
 * URI-based user authentication using the HTTP basic method.
 *
 * Copyright 2006-2007 Willy Tarreau <w@1wt.eu>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 */

#include <stdlib.h>
#include <string.h>

#include <haproxy/acl.h>
#include <haproxy/action.h>
#include <haproxy/api.h>
#include <haproxy/auth.h>
#include <haproxy/base64.h>
#include <haproxy/errors.h>
#include <haproxy/list.h>
#include <haproxy/stats-t.h>
#include <haproxy/uri_auth.h>


/*
 * Initializes a basic uri_auth structure header and returns a pointer to it.
 * Uses the pointer provided if not NULL and not initialized.
 */
struct uri_auth *stats_check_init_uri_auth(struct uri_auth **root)
{
	struct uri_auth *u;

	if (!root || !*root) {
		if ((u = calloc(1, sizeof (*u))) == NULL)
			goto out_u;

		LIST_INIT(&u->http_req_rules);
		LIST_INIT(&u->admin_rules);
		stats_uri_auth_take(u);
	} else
		u = *root;

	if (!u->uri_prefix) {
		u->uri_len = strlen(STATS_DEFAULT_URI);
		if ((u->uri_prefix = strdup(STATS_DEFAULT_URI)) == NULL)
			goto out_uri;
	}

	if (root && !*root)
		*root = u;

	return u;

 out_uri:
	if (!root || !*root)
		free(u);
 out_u:
	return NULL;
}

/*
 * Returns a default uri_auth with <uri> set as the uri_prefix.
 * Uses the pointer provided if not NULL and not initialized.
 */
struct uri_auth *stats_set_uri(struct uri_auth **root, char *uri)
{
	struct uri_auth *u;
	char *uri_copy;
	int uri_len;

	uri_len  = strlen(uri);
	if ((uri_copy = strdup(uri)) == NULL)
		goto out_uri;
	
	if ((u = stats_check_init_uri_auth(root)) == NULL)
		goto out_u;
	
	free(u->uri_prefix);
	u->uri_prefix = uri_copy;
	u->uri_len = uri_len;
	return u;

 out_u:
	free(uri_copy);
 out_uri:
	return NULL;
}

/*
 * Returns a default uri_auth with <realm> set as the realm.
 * Uses the pointer provided if not NULL and not initialized.
 */
struct uri_auth *stats_set_realm(struct uri_auth **root, char *realm)
{
	struct uri_auth *u;
	char *realm_copy;

	if ((realm_copy = strdup(realm)) == NULL)
		goto out_realm;
	
	if ((u = stats_check_init_uri_auth(root)) == NULL)
		goto out_u;
	
	free(u->auth_realm);
	u->auth_realm = realm_copy;
	return u;

 out_u:
	free(realm_copy);
 out_realm:
	return NULL;
}

/*
 * Returns a default uri_auth with STAT_F_SHNODE flag enabled and
 * <node> set as the name if it is not empty.
 * Uses the pointer provided if not NULL and not initialized.
 */
struct uri_auth *stats_set_node(struct uri_auth **root, char *name)
{
	struct uri_auth *u;
	char *node_copy = NULL;

	if (name && *name) {
		node_copy = strdup(name);
		if (node_copy == NULL)
			goto out_realm;
	}
	
	if ((u = stats_check_init_uri_auth(root)) == NULL)
		goto out_u;

	if (!stats_set_flag(root, STAT_F_SHNODE))
		goto out_u;

	if (node_copy) {	
		free(u->node);
		u->node = node_copy;
	}

	return u;

 out_u:
	free(node_copy);
 out_realm:
	return NULL;
}

/*
 * Returns a default uri_auth with STAT_F_SHDESC flag enabled and
 * <description> set as the desc if it is not empty.
 * Uses the pointer provided if not NULL and not initialized.
 */
struct uri_auth *stats_set_desc(struct uri_auth **root, char *desc)
{
	struct uri_auth *u;
	char *desc_copy = NULL;

	if (desc && *desc) {
		desc_copy = strdup(desc);
		if (desc_copy == NULL)
			goto out_realm;
	}
	
	if ((u = stats_check_init_uri_auth(root)) == NULL)
		goto out_u;

	if (!stats_set_flag(root, STAT_F_SHDESC))
		goto out_u;

	if (desc_copy) {
		free(u->desc);
		u->desc = desc_copy;
	}

	return u;

 out_u:
	free(desc_copy);
 out_realm:
	return NULL;
}

/*
 * Returns a default uri_auth with the <refresh> refresh interval.
 * Uses the pointer provided if not NULL and not initialized.
 */
struct uri_auth *stats_set_refresh(struct uri_auth **root, int interval)
{
	struct uri_auth *u;
	
	if ((u = stats_check_init_uri_auth(root)) != NULL)
		u->refresh = interval;
	return u;
}

/*
 * Returns a default uri_auth with the <flag> set.
 * Uses the pointer provided if not NULL and not initialized.
 */
struct uri_auth *stats_set_flag(struct uri_auth **root, int flag)
{
	struct uri_auth *u;
	
	if ((u = stats_check_init_uri_auth(root)) != NULL)
		u->flags |= flag;
	return u;
}

/*
 * Returns a default uri_auth with a <user:passwd> entry added to the list of
 * authorized users. If a matching entry is found, no update will be performed.
 * Uses the pointer provided if not NULL and not initialized.
 */
struct uri_auth *stats_add_auth(struct uri_auth **root, char *user)
{
	struct uri_auth *u;
	struct auth_users *newuser;
	char *pass;

	pass = strchr(user, ':');
	if (pass)
		*pass++ = '\0';
	else
		pass = "";

	if ((u = stats_check_init_uri_auth(root)) == NULL)
		return NULL;

	if (!u->userlist)
		u->userlist = calloc(1, sizeof(*u->userlist));

	if (!u->userlist)
		return NULL;

	if (!u->userlist->name)
		u->userlist->name = strdup(".internal-stats-userlist");

	if (!u->userlist->name)
		return NULL;

	for (newuser = u->userlist->users; newuser; newuser = newuser->next)
		if (strcmp(newuser->user, user) == 0) {
			ha_warning("uri auth: ignoring duplicated user '%s'.\n",
				   user);
			return u;
		}

	newuser = calloc(1, sizeof(*newuser));
	if (!newuser)
		return NULL;

	newuser->user = strdup(user);
	if (!newuser->user) {
		free(newuser);
		return NULL;
	}

	newuser->pass = strdup(pass);
	if (!newuser->pass) {
		free(newuser->user);
		free(newuser);
		return NULL;
	}

	newuser->flags |= AU_O_INSECURE;
	newuser->next = u->userlist->users;
	u->userlist->users = newuser;

	return u;
}

/*
 * Returns a default uri_auth with a <scope> entry added to the list of
 * allowed scopes. If a matching entry is found, no update will be performed.
 * Uses the pointer provided if not NULL and not initialized.
 */
struct uri_auth *stats_add_scope(struct uri_auth **root, char *scope)
{
	struct uri_auth *u;
	char *new_name;
	struct stat_scope *old_scope, **scope_list;

	if ((u = stats_check_init_uri_auth(root)) == NULL)
		goto out;

	scope_list = &u->scope;
	while ((old_scope = *scope_list)) {
		if (strcmp(old_scope->px_id, scope) == 0)
			break;
		scope_list = &old_scope->next;
	}

	if (!old_scope) {
		if ((new_name = strdup(scope)) == NULL)
			goto out_u;

		if ((old_scope = calloc(1, sizeof(*old_scope))) == NULL)
			goto out_name;

		old_scope->px_id = new_name;
		old_scope->px_len = strlen(new_name);
		*scope_list = old_scope;
	}
	return u;

 out_name:
	free(new_name);
 out_u:
	free(u);
 out:
	return NULL;
}

void stats_uri_auth_free(struct uri_auth *uri_auth)
{
	struct stat_scope *scope, *scopep;
	struct stats_admin_rule *rule, *ruleb;

	free(uri_auth->uri_prefix);
	free(uri_auth->auth_realm);
	free(uri_auth->node);
	free(uri_auth->desc);

	userlist_free(uri_auth->userlist);
	free_act_rules(&uri_auth->http_req_rules);
	list_for_each_entry_safe(rule, ruleb, &uri_auth->admin_rules, list) {
		LIST_DELETE(&rule->list);
		free_acl_cond(rule->cond);
		free(rule);
	}

	scope = uri_auth->scope;
	while (scope) {
		scopep = scope;
		scope = scope->next;
		free(scopep->px_id);
		free(scopep);
	}

	free(uri_auth);
}

void stats_uri_auth_drop(struct uri_auth *uri_auth)
{
	if (!uri_auth)
		return;
	if (HA_ATOMIC_SUB_FETCH(&uri_auth->refcount, 1) == 0)
		stats_uri_auth_free(uri_auth);
}

void stats_uri_auth_take(struct uri_auth *uri_auth)
{
	if (!uri_auth)
		return;
	HA_ATOMIC_INC(&uri_auth->refcount);
}

/*
 * Local variables:
 *  c-indent-level: 8
 *  c-basic-offset: 8
 * End:
 */
