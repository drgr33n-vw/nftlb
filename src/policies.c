/*
 *   This file is part of nftlb, nftables load balancer.
 *
 *   Copyright (C) ZEVENET SL.
 *   Author: Laura Garcia <laura.garcia@zevenet.com>
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU Affero General Public License as
 *   published by the Free Software Foundation, either version 3 of the
 *   License, or any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU Affero General Public License for more details.
 *
 *   You should have received a copy of the GNU Affero General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "policies.h"
#include "elements.h"
#include "objects.h"
#include "config.h"
#include "nft.h"
#include "tools.h"

static struct policy * policy_create(char *name)
{
	struct list_head *policies = obj_get_policies();

	struct policy *p = (struct policy *)malloc(sizeof(struct policy));
	if (!p) {
		tools_printlog(LOG_ERR, "Policy memory allocation error");
		return NULL;
	}

	obj_set_attribute_string(name, &p->name);

	p->type = DEFAULT_POLICY_TYPE;
	p->route = DEFAULT_POLICY_ROUTE;
	p->family = DEFAULT_FAMILY;
	p->timeout = DEFAULT_POLICY_TIMEOUT;
	p->used = 0;
	p->logprefix = DEFAULT_POLICY_LOGPREFIX;
	p->action = DEFAULT_ACTION;

	init_list_head(&p->elements);

	p->total_elem = 0;

	list_add_tail(&p->list, policies);
	obj_set_total_policies(obj_get_total_policies() + 1);

	return p;
}

static int policy_delete(struct policy *p)
{
	if (!p)
		return 0;

	list_del(&p->list);

	if (p->name)
		free(p->name);
	if (p->logprefix && strcmp(p->logprefix, DEFAULT_POLICY_LOGPREFIX) != 0)
		free(p->logprefix);

	free(p);
	obj_set_total_policies(obj_get_total_policies() - 1);

	return 0;
}

static int policy_set_family(struct policy *p, int new_value)
{
	int old_value = p->family;

	tools_printlog(LOG_DEBUG, "%s():%d: policy %s old family %d new family %d", __FUNCTION__, __LINE__, p->name, old_value, new_value);

	if (new_value != VALUE_FAMILY_IPV4 && new_value != VALUE_FAMILY_IPV6) {
		tools_printlog(LOG_INFO, "%s():%d: family %d not supported for policies", __FUNCTION__, __LINE__, new_value);
		return 0;
	}

	if (old_value == new_value) {
		tools_printlog(LOG_DEBUG, "%s():%d: family %d without change for policy %s", __FUNCTION__, __LINE__, p->family, p->name);
		return 0;
	}

	p->family = new_value;

	return 0;
}

static int policy_set_type(struct policy *p, int new_value)
{
	int old_value = p->type;

	tools_printlog(LOG_DEBUG, "%s():%d: policy %s old type %d new type %d", __FUNCTION__, __LINE__, p->name, old_value, new_value);

	p->type = new_value;

	return 0;
}

void policy_print(struct policy *p)
{
	tools_printlog(LOG_DEBUG," [policy] ");
	tools_printlog(LOG_DEBUG,"    [%s] %s", CONFIG_KEY_NAME, p->name);
	tools_printlog(LOG_DEBUG,"    [%s] %s", CONFIG_KEY_TYPE, obj_print_policy_type(p->type));

	tools_printlog(LOG_DEBUG,"    [%s] %s", CONFIG_KEY_ROUTE, obj_print_policy_route(p->route));
	tools_printlog(LOG_DEBUG,"    [%s] %s", CONFIG_KEY_FAMILY, obj_print_family(p->family));
	tools_printlog(LOG_DEBUG,"    [%s] %d", CONFIG_KEY_TIMEOUT, p->timeout);
	if (p->logprefix)
		tools_printlog(LOG_DEBUG,"    [%s] %s", CONFIG_KEY_LOGPREFIX, p->logprefix);

	tools_printlog(LOG_DEBUG,"    *[used] %d", p->used);
	tools_printlog(LOG_DEBUG,"    *[total_elem] %d", p->total_elem);
	tools_printlog(LOG_DEBUG,"    *[%s] %d", CONFIG_KEY_ACTION, p->action);

	if (p->total_elem != 0)
		element_s_print(p);
}

void policies_s_print(void)
{
	struct list_head *policies = obj_get_policies();
	struct policy *p;

	if (!policies)
		return;

	list_for_each_entry(p, policies, list) {
		policy_print(p);
	}
}

struct policy * policy_lookup_by_name(const char *name)
{
	struct list_head *policies = obj_get_policies();
	struct policy *p;

	list_for_each_entry(p, policies, list) {
		if (strcmp(p->name, name) == 0)
			return p;
	}

	return NULL;
}

int policy_changed(struct config_pair *c)
{
	struct policy *p = obj_get_current_policy();

	if (!p)
		return -1;

	syslog(LOG_DEBUG, "%s():%d: policy %s with param %d", __FUNCTION__, __LINE__, p->name, c->key);

	switch (c->key) {
	case KEY_NAME:
		return 1;
		break;
	case KEY_TYPE:
		return !obj_equ_attribute_int(p->type, c->int_value);
		break;
	case KEY_FAMILY:
		return !obj_equ_attribute_int(p->family, c->int_value);
		break;
	case KEY_TIMEOUT:
		return !obj_equ_attribute_int(p->timeout, c->int_value);
		break;
	case KEY_LOGPREFIX:
		return !obj_equ_attribute_string(p->logprefix, c->str_value);
		break;
	default:
		break;
	}

	return 0;
}

int policy_set_attribute(struct config_pair *c)
{
	struct policy *p = obj_get_current_policy();

	if (c->key != KEY_NAME && !p)
		return PARSER_OBJ_UNKNOWN;

	switch (c->key) {
	case KEY_NAME:
		p = policy_lookup_by_name(c->str_value);
		if (!p) {
			p = policy_create(c->str_value);
			if (!p)
				return -1;
			policy_set_action(p, ACTION_START);
		} else {
			policy_set_action(p, ACTION_RELOAD);
		}
		obj_set_current_policy(p);
		break;
	case KEY_TYPE:
		policy_set_type(p, c->int_value);
		break;
	case KEY_ROUTE:
		p->route = c->int_value;
		break;
	case KEY_FAMILY:
		policy_set_family(p, c->int_value);
		break;
	case KEY_TIMEOUT:
		p->timeout = c->int_value;
		break;
	case KEY_ACTION:
		policy_set_action(p, c->int_value);
		break;
	case KEY_LOGPREFIX:
		if (strcmp(p->logprefix, DEFAULT_POLICY_LOGPREFIX) != 0)
			free(p->logprefix);
		obj_set_attribute_string(c->str_value, &p->logprefix);
		break;
	case KEY_USED:
		break;
	default:
		return PARSER_STRUCT_FAILED;
	}

	return PARSER_OK;
}

int policy_set_action(struct policy *p, int action)
{
	tools_printlog(LOG_DEBUG, "%s():%d: policy %s set action %d", __FUNCTION__, __LINE__, p->name, action);

	if (p->action == action || (p->action == ACTION_START && action == ACTION_RELOAD))
		return 0;

	if (action == ACTION_DELETE) {
		farm_s_lookup_policy_action(p->name, action);
		policy_delete(p);
		return 1;
	}

	if (action == ACTION_STOP || action == ACTION_RELOAD) {
		farm_s_lookup_policy_action(p->name, action);
		address_s_lookup_policy_action(p->name, action);
	}

	p->action = action;

	return 1;
}

int policy_s_set_action(int action)
{
	struct list_head *policies = obj_get_policies();
	struct policy *p, *next;

	list_for_each_entry_safe(p, next, policies, list)
		policy_set_action(p, action);

	return 0;
}

int policy_pre_actionable(struct config_pair *c)
{
	struct policy *p = obj_get_current_policy();

	if (!p)
		return -1;

	tools_printlog(LOG_DEBUG, "%s():%d: pos actionable policy %s with param %d action is %d", __FUNCTION__, __LINE__, p->name, c->key, p->action);

	switch (c->key) {
	case KEY_NAME:
		break;
	case KEY_FAMILY:
	case KEY_ROUTE:
	case KEY_TIMEOUT:
		policy_set_action(p, ACTION_STOP);
		break;
	case KEY_USED:
		break;
	case KEY_TYPE:
	default:
		policy_set_action(p, ACTION_RELOAD);
	}

	return 0;
}

int policy_pos_actionable(struct config_pair *c)
{
	struct policy *p = obj_get_current_policy();

	if (!p)
		return -1;

	tools_printlog(LOG_DEBUG, "%s():%d: pos actionable policy %s with param %d action is %d", __FUNCTION__, __LINE__, p->name, c->key, p->action);

	switch (c->key) {
	case KEY_NAME:
		break;
	case KEY_FAMILY:
	case KEY_TYPE:
	case KEY_ROUTE:
	case KEY_TIMEOUT:
		policy_set_action(p, ACTION_START);
		break;
	case KEY_USED:
		break;
	default:
		policy_set_action(p, ACTION_RELOAD);
	}

	return 0;
}

int policy_rulerize(struct policy *p)
{
	int ret = 0;
	tools_printlog(LOG_DEBUG, "%s():%d: rulerize policy %s", __FUNCTION__, __LINE__, p->name);

	policy_print(p);

	if (p->action == ACTION_NONE) {
		tools_printlog(LOG_INFO, "%s():%d: policy %s won't be rulerized", __FUNCTION__, __LINE__, p->name);
		return 0;
	}

	ret = nft_rulerize_policies(p);
	element_s_delete(p);
	return ret;
}

int policy_s_rulerize(void)
{
	struct policy *p;
	int ret = 0;
	int output = 0;

	tools_printlog(LOG_DEBUG, "%s():%d: rulerize all policies", __FUNCTION__, __LINE__);

	struct list_head *policies = obj_get_policies();

	list_for_each_entry(p, policies, list) {
		ret = policy_rulerize(p);
		output = output || ret;
	}

	return output;
}
