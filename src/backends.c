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
#include <net/if.h>

#include "backends.h"
#include "farms.h"
#include "farmaddress.h"
#include "objects.h"
#include "network.h"
#include "sessions.h"
#include "tools.h"

#define BACKEND_MARK_MIN			0x00000001
#define BACKEND_MARK_MAX			0x00000FFF

static int backend_gen_next_mark(void)
{
	struct list_head *farms = obj_get_farms();
	struct farm *f;
	struct backend *b;
	int mark = BACKEND_MARK_MIN;
	int found = 0;

	for (mark = BACKEND_MARK_MIN; mark <= BACKEND_MARK_MAX; mark++) {
		found = 0;
		list_for_each_entry(f, farms, list) {
			list_for_each_entry(b, &f->backends, list) {
				if (mark == b->mark) {
					found = 1;
					continue;
				}
			}
			if (found)
				continue;
		}

		if (!found)
			return mark;
	}

	return DEFAULT_MARK;
}

static struct backend * backend_create(struct farm *f, char *name)
{
	struct backend *b = (struct backend *)malloc(sizeof(struct backend));
	if (!b) {
		tools_printlog(LOG_ERR, "Backend memory allocation error");
		return NULL;
	}

	b->parent = f;
	obj_set_attribute_string(name, &b->name);

	b->fqdn = DEFAULT_FQDN;
	b->ethaddr = DEFAULT_ETHADDR;
	b->oface = DEFAULT_IFNAME;
	b->ofidx = DEFAULT_IFIDX;
	b->ipaddr = DEFAULT_IPADDR;
	b->port = DEFAULT_PORT;
	b->srcaddr = DEFAULT_SRCADDR;
	b->weight = DEFAULT_WEIGHT;
	b->priority = DEFAULT_PRIORITY;
	b->mark = backend_gen_next_mark();
	b->estconnlimit = DEFAULT_ESTCONNLIMIT;
	b->estconnlimit_logprefix = DEFAULT_B_ESTCONNLIMIT_LOGPREFIX;
	b->state = DEFAULT_BACKEND_STATE;
	b->action = DEFAULT_ACTION;

	b->parent->bcks_have_port = 0;

	list_add_tail(&b->list, &f->backends);
	f->total_bcks++;

	return b;
}

static int backend_delete_node(struct backend *b)
{
	list_del(&b->list);
	if (b->name)
		free(b->name);
	if (b->fqdn && strcmp(b->fqdn, "") != 0)
		free(b->fqdn);
	if (b->oface && strcmp(b->oface, "") != 0)
		free(b->oface);
	if (b->ipaddr && strcmp(b->ipaddr, "") != 0)
		free(b->ipaddr);
	if (b->ethaddr && strcmp(b->ethaddr, "") != 0)
		free(b->ethaddr);
	if (b->port && strcmp(b->port, "") != 0)
		free(b->port);
	if (b->srcaddr && strcmp(b->srcaddr, "") != 0)
		free(b->srcaddr);
	if (b->estconnlimit_logprefix && strcmp(b->estconnlimit_logprefix, DEFAULT_B_ESTCONNLIMIT_LOGPREFIX) != 0)
		free(b->estconnlimit_logprefix);

	free(b);

	return 0;
}

static int backend_below_prio(struct backend *b)
{
	struct farm *f = b->parent;

	tools_printlog(LOG_DEBUG, "%s():%d: backend %s state is %s and priority %d <= farm prio %d",
				   __FUNCTION__, __LINE__, b->name, obj_print_state(b->state), b->priority, f->priority);

	return (b->priority <= f->priority);
}

static int backend_s_set_ports(struct farm *f)
{
	struct backend *b;

	tools_printlog(LOG_DEBUG, "%s():%d: finding backends with port for %s", __FUNCTION__, __LINE__, f->name);

	list_for_each_entry(b, &f->backends, list) {
		if (strcmp(b->port, DEFAULT_PORT) == 0) {
			f->bcks_have_port = 0;
			return 0;
		}
	}

	f->bcks_have_port = 1;
	return 1;
}

static int backend_delete(struct backend *b)
{
	if (!b)
		return 0;

	struct farm *f = b->parent;
	backend_set_action(b, ACTION_STOP);
	session_backend_action(f, b, ACTION_STOP);

	if (backend_below_prio(b)) {
		backend_s_gen_priority(f, ACTION_DELETE);
		obj_rulerize(OBJ_START);
	}

	session_backend_action(f, b, ACTION_DELETE);
	backend_delete_node(b);
	backend_s_set_ports(f);

	if (backend_s_gen_priority(f, ACTION_DELETE)) {
		farm_set_action(f, ACTION_RELOAD);
		farmaddress_s_set_action(f, ACTION_RELOAD);
		obj_rulerize(OBJ_START);
	}

	return 0;
}

void backend_s_print(struct farm *f)
{
	struct backend *b;

	list_for_each_entry(b, &f->backends, list) {
		tools_printlog(LOG_DEBUG,"    [backend] ");
		tools_printlog(LOG_DEBUG,"       [%s] %s", CONFIG_KEY_NAME, b->name);

		if (b->fqdn)
			tools_printlog(LOG_DEBUG,"       [%s] %s", CONFIG_KEY_FQDN, b->fqdn);

		if (b->oface)
			tools_printlog(LOG_DEBUG,"       [%s] %s", CONFIG_KEY_OFACE, b->oface);

		tools_printlog(LOG_DEBUG,"      *[ofidx] %d", b->ofidx);

		if (b->ipaddr)
			tools_printlog(LOG_DEBUG,"       [%s] %s", CONFIG_KEY_IPADDR, b->ipaddr);

		if (b->ethaddr)
			tools_printlog(LOG_DEBUG,"       [%s] %s", CONFIG_KEY_ETHADDR, b->ethaddr);

		if (b->port)
			tools_printlog(LOG_DEBUG,"       [%s] %s", CONFIG_KEY_PORT, b->port);

		if (b->srcaddr)
			tools_printlog(LOG_DEBUG,"       [%s] %s", CONFIG_KEY_SRCADDR, b->srcaddr);

		tools_printlog(LOG_DEBUG,"       [%s] 0x%x", CONFIG_KEY_MARK, b->mark);
		tools_printlog(LOG_DEBUG,"       [%s] %d", CONFIG_KEY_ESTCONNLIMIT, b->estconnlimit);
		if (b->estconnlimit_logprefix && strcmp(b->estconnlimit_logprefix, DEFAULT_B_ESTCONNLIMIT_LOGPREFIX) != 0)
			tools_printlog(LOG_DEBUG,"       [%s] %s", CONFIG_KEY_ESTCONNLIMIT_LOGPREFIX, b->estconnlimit_logprefix);

		tools_printlog(LOG_DEBUG,"       [%s] %d", CONFIG_KEY_WEIGHT, b->weight);
		tools_printlog(LOG_DEBUG,"       [%s] %d", CONFIG_KEY_PRIORITY, b->priority);
		tools_printlog(LOG_DEBUG,"       [%s] %s", CONFIG_KEY_STATE, obj_print_state(b->state));
		tools_printlog(LOG_DEBUG,"      *[%s] %d", CONFIG_KEY_ACTION, b->action);
	}
}

struct backend * backend_lookup_by_key(struct farm *f, int key, const char *name, int value)
{
	struct backend *b;

	tools_printlog(LOG_DEBUG, "%s():%d: farm %s key %d name %s value %d", __FUNCTION__, __LINE__, f->name, key, name, value);

	list_for_each_entry(b, &f->backends, list) {
		switch (key) {
		case KEY_NAME:
			if (strcmp(b->name, name) == 0)
				return b;
			break;
		case KEY_MARK:
			if (value == backend_get_mark(b))
				return b;
			break;
		case KEY_ETHADDR:
			if (b->ethaddr && strcmp(b->ethaddr, name) == 0)
				return b;
			break;
		case KEY_IPADDR:
			if (b->ipaddr && strcmp(b->ipaddr, name) == 0)
				return b;
			break;
		default:
			return NULL;
		}
	}

	return NULL;
}

static int backend_set_ipaddr_from_ether(struct backend *b)
{
	struct farm *f = b->parent;
	struct farmaddress *fa;
	struct address *a;
	int ret = -1;
	unsigned char dst_ethaddr[ETH_HW_ADDR_LEN];
	unsigned char src_ethaddr[ETH_HW_ADDR_LEN];
	char streth[ETH_HW_STR_LEN] = {};
	int *oface;
	char **source_ip;

	if (!farm_is_ingress_mode(f) || (f->state != VALUE_STATE_UP && f->state != VALUE_STATE_CONFERR))
		return 0;

	fa = farmaddress_get_first(f);
	if (!fa) {
		tools_printlog(LOG_INFO, "%s():%d: no farm address configured in %s", __FUNCTION__, __LINE__, f->name);
		return -1;
	}

	a = fa->address;
	if (a->iethaddr == DEFAULT_ETHADDR ||
		b->ipaddr == DEFAULT_IPADDR ||
		fa->farm->ofidx == DEFAULT_IFIDX)
		return -1;

	sscanf(a->iethaddr, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", &src_ethaddr[0], &src_ethaddr[1], &src_ethaddr[2], &src_ethaddr[3], &src_ethaddr[4], &src_ethaddr[5]);

	oface = &a->ifidx;

	source_ip = &(a->ipaddr);

	if (f->srcaddr != DEFAULT_SRCADDR)
		source_ip = &f->srcaddr;

	ret = net_get_neigh_ether((unsigned char **) &dst_ethaddr, src_ethaddr, a->family, *source_ip, b->ipaddr, *oface);

	if (ret != 0) {
		oface = &f->ofidx;

		if (b->ofidx != DEFAULT_IFIDX)
			oface = &b->ofidx;

		if (f->srcaddr != DEFAULT_SRCADDR)
			source_ip = &f->srcaddr;

		if (b->srcaddr != DEFAULT_SRCADDR)
			source_ip = &b->srcaddr;

		ret = net_get_neigh_ether((unsigned char **) &dst_ethaddr, src_ethaddr, a->family, *source_ip, b->ipaddr, *oface);
	}

	if (ret == 0) {
		sprintf(streth, "%02x:%02x:%02x:%02x:%02x:%02x", dst_ethaddr[0],
			dst_ethaddr[1], dst_ethaddr[2], dst_ethaddr[3], dst_ethaddr[4], dst_ethaddr[5]);

		tools_printlog(LOG_DEBUG, "%s():%d: discovered ether address for %s is %s", __FUNCTION__, __LINE__, b->name, streth);

		if (b->ethaddr)
			free(b->ethaddr);
		obj_set_attribute_string(streth, &b->ethaddr);
	}

	return ret;
}

static int backend_set_weight(struct backend *b, int new_value)
{
	struct farm *f = b->parent;
	int old_value = b->weight;

	tools_printlog(LOG_DEBUG, "%s():%d: current value is %d, but new value will be %d",
				   __FUNCTION__, __LINE__, old_value, new_value);

	b->weight = new_value;

	if (backend_is_available(b))
		f->total_weight += (b->weight-old_value);

	return 0;
}

static int backend_set_estconnlimit(struct backend *b, int new_value)
{
	int old_value = b->estconnlimit;

	tools_printlog(LOG_DEBUG, "%s():%d: current value is %d, but new value will be %d",
				   __FUNCTION__, __LINE__, old_value, new_value);

	if (new_value == old_value)
		return 0;

	b->estconnlimit = new_value;

	return 0;
}

static void backend_s_update_counters(struct farm *f)
{
	struct backend *bp, *next;

	tools_printlog(LOG_DEBUG, "%s():%d: farm %s", __FUNCTION__, __LINE__, f->name);

	f->bcks_available = 0;
	f->bcks_usable = 0;
	f->total_weight = 0;

	list_for_each_entry_safe(bp, next, &f->backends, list) {
		if (backend_is_available(bp)) {
			f->bcks_available++;
			f->total_weight += bp->weight;
		}
		if (backend_is_usable(bp))
			f->bcks_usable++;
	}
}

static int backend_set_priority(struct backend *b, int new_value)
{
	int old_value = b->priority;

	tools_printlog(LOG_DEBUG, "%s():%d: current value is %d, but new value will be %d",
				   __FUNCTION__, __LINE__, old_value, new_value);

	if (new_value <= 0)
		return -1;

	b->priority = new_value;
	backend_s_gen_priority(b->parent, ACTION_RELOAD);

	return 0;
}

static int backend_s_set_srcaddr(struct farm *f)
{
	struct backend *b;

	tools_printlog(LOG_DEBUG, "%s():%d: finding backends with srouce address for %s", __FUNCTION__, __LINE__, f->name);

	list_for_each_entry(b, &f->backends, list) {
		if (b->srcaddr && strcmp(b->srcaddr, "") != 0) {
			f->bcks_have_srcaddr = 1;
			return 1;
		}
	}

	f->bcks_have_srcaddr = 0;
	return 0;
}

static int backend_set_mark(struct backend *b, int new_value)
{
	int old_value = b->mark;

	if (new_value < BACKEND_MARK_MIN || new_value > BACKEND_MARK_MAX)
		return 0;

	tools_printlog(LOG_DEBUG, "%s():%d: current value is %d, but new value will be %d",
				   __FUNCTION__, __LINE__, old_value, new_value);

	b->mark = new_value;

	return 0;
}

static int backend_set_port(struct backend *b, char *new_value)
{
	char *old_value = b->port;

	tools_printlog(LOG_DEBUG, "%s():%d: current value is %s, but new value will be %s",
				   __FUNCTION__, __LINE__, old_value, new_value);

	if (strcmp(b->port, DEFAULT_PORT) != 0)
		free(b->port);
	obj_set_attribute_string(new_value, &b->port);

	if (b->parent->bcks_have_port) {
		if (strcmp(b->port, DEFAULT_PORT) == 0)
			b->parent->bcks_have_port = 0;
	} else
		backend_s_set_ports(b->parent);

	return 0;
}

static int backend_set_srcaddr(struct backend *b, char *new_value)
{
	char *old_value = b->srcaddr;

	tools_printlog(LOG_DEBUG, "%s():%d: current value is %s, but new value will be %s",
				   __FUNCTION__, __LINE__, old_value, new_value);

	if (b->srcaddr)
		free(b->srcaddr);
	obj_set_attribute_string(new_value, &b->srcaddr);

	if (b->srcaddr && strcmp(b->srcaddr, "") != 0)
		b->parent->bcks_have_srcaddr = 1;
	else
		backend_s_set_srcaddr(b->parent);

	return 0;
}

static int backend_set_ifinfo(struct backend *b)
{
	struct farm *f = b->parent;
	char if_str[IFNAMSIZ];
	int if_index;
	int ret = 0;

	tools_printlog(LOG_DEBUG, "%s():%d: backend %s set interface info", __FUNCTION__, __LINE__, b->name);

	if (!farm_is_ingress_mode(f) || (f->state != VALUE_STATE_UP && f->state != VALUE_STATE_CONFERR))
		return 0;

	if (f->oface && strcmp(f->oface, IFACE_LOOPBACK) == 0) {
		tools_printlog(LOG_DEBUG, "%s():%d: backend %s in farm %s doesn't require output netinfo, loopback interface", __FUNCTION__, __LINE__, b->name, f->name);
		f->ofidx = 0;
		return 0;
	}

	if (!b || b->ipaddr == DEFAULT_IPADDR) {
		tools_printlog(LOG_ERR, "%s():%d: there is no backend yet in the farm %s", __FUNCTION__, __LINE__, f->name);
		return 0;
	}

	ret = net_get_local_ifidx_per_remote_host(b->ipaddr, &if_index);
	if (ret == -1) {
		tools_printlog(LOG_ERR, "%s():%d: unable to get the outbound interface to %s for the backend %s in farm %s", __FUNCTION__, __LINE__, b->ipaddr, b->name, f->name);
		return -1;
	}

	if (f->ofidx == -1)
		f->ofidx = if_index;

	if (f->ofidx != if_index) {
		f->bcks_have_if = 1;
		b->ofidx = if_index;
	}

	if (if_indextoname(if_index, if_str) == NULL) {
		tools_printlog(LOG_ERR, "%s():%d: unable to get the outbound interface name with index %d required by the backend %s in farm %s", __FUNCTION__, __LINE__, if_index, b->name, f->name);
		return -1;
	}

	if (!f->oface) {
		obj_set_attribute_string(if_str, &f->oface);
		net_strim_netface(f->oface);
		return 0;
	}

	if (f->oface && strcmp(f->oface, if_str) != 0) {
		free(b->oface);
		obj_set_attribute_string(if_str, &b->oface);
		net_strim_netface(b->oface);
	}

	return 0;
}

static int backend_set_ipaddr(struct backend *b, char *new_value)
{
	char *old_value = b->ipaddr;
	int netconfig;

	tools_printlog(LOG_DEBUG, "%s():%d: current value is %s, but new value will be %s",
				   __FUNCTION__, __LINE__, old_value, new_value);

	if (b->ipaddr)
		free(b->ipaddr);
	if (b->ethaddr)
		free(b->ethaddr);
	obj_set_attribute_string(new_value, &b->ipaddr);
	obj_set_attribute_string("", &b->ethaddr);

	netconfig = (backend_set_ifinfo(b) == 0 && backend_set_ipaddr_from_ether(b) == 0);

	if (old_value == DEFAULT_IPADDR)
		return 0;

	if (netconfig) {
		if (b->state == VALUE_STATE_CONFERR)
			backend_set_state(b, VALUE_STATE_UP);
	} else
		backend_set_state(b, VALUE_STATE_CONFERR);

	return 0;
}

int backend_is_usable(struct backend *b)
{
	tools_printlog(LOG_DEBUG, "%s():%d: backend %s state is %s and priority %d",
				   __FUNCTION__, __LINE__, b->name, obj_print_state(b->state), b->priority);

	return ((b->state == VALUE_STATE_UP || b->state == VALUE_STATE_OFF) && backend_below_prio(b));
}

int backend_no_port(struct backend *b)
{
	if (obj_equ_attribute_string(b->port, DEFAULT_PORT))
		return 1;
	return 0;
}

int backend_changed(struct config_pair *c)
{
	struct farm *f = obj_get_current_farm();
	struct backend *b = obj_get_current_backend();

	if (!f || !b)
		return -1;

	tools_printlog(LOG_DEBUG, "%s():%d: farm %s backend %s with param %d", __FUNCTION__, __LINE__, f->name, b->name, c->key);

	switch (c->key) {
	case KEY_NAME:
		return 1;
		break;
	case KEY_NEWNAME:
		return !obj_equ_attribute_string(b->name, c->str_value);
		break;
	case KEY_FQDN:
		return !obj_equ_attribute_string(b->fqdn, c->str_value);
		break;
	case KEY_IPADDR:
		return !obj_equ_attribute_string(b->ipaddr, c->str_value);
		break;
	case KEY_ETHADDR:
		return !obj_equ_attribute_string(b->ethaddr, c->str_value);
		break;
	case KEY_PORT:
		return !obj_equ_attribute_string(b->port, c->str_value);
		break;
	case KEY_SRCADDR:
		return !obj_equ_attribute_string(b->srcaddr, c->str_value);
		break;
	case KEY_WEIGHT:
		return !obj_equ_attribute_int(b->weight, c->int_value);
		break;
	case KEY_PRIORITY:
		return !obj_equ_attribute_int(b->priority, c->int_value);
		break;
	case KEY_MARK:
		return !obj_equ_attribute_int(b->mark, c->int_value);
		break;
	case KEY_STATE:
		return !obj_equ_attribute_int(b->state, c->int_value);
		break;
	case KEY_ACTION:
		return !obj_equ_attribute_int(b->action, c->int_value);
		break;
	case KEY_ESTCONNLIMIT:
		return !obj_equ_attribute_int(b->estconnlimit, c->int_value);
		break;
	case KEY_ESTCONNLIMIT_LOGPREFIX:
		return !obj_equ_attribute_string(b->estconnlimit_logprefix, c->str_value);
		break;
	default:
		break;
	}

	return 0;
}

int backend_validate(struct backend *b)
{
	struct farm *f = b->parent;

	tools_printlog(LOG_DEBUG, "%s():%d: validating backend %s of farm %s",
				   __FUNCTION__, __LINE__, b->name, f->name);

	if (farm_is_ingress_mode(f) &&
		(!b->ethaddr || strcmp(b->ethaddr, "") == 0))
		return 0;

	if (!b->ipaddr || strcmp(b->ipaddr, "") == 0)
		return 0;

	return 1;
}

int backend_is_available(struct backend *b)
{
	tools_printlog(LOG_DEBUG, "%s():%d: backend %s state is %s and priority %d",
				   __FUNCTION__, __LINE__, b->name, obj_print_state(b->state), b->priority);

	return (backend_validate(b) && (b->state == VALUE_STATE_UP) && backend_below_prio(b));
}

int backend_set_action(struct backend *b, int action)
{
	int is_actionated = 0;

	tools_printlog(LOG_DEBUG, "%s():%d: bck %s action %d state %d - new action %d",
				   __FUNCTION__, __LINE__, b->name, b->action, b->state, action);

	if (action == ACTION_DELETE) {
		backend_delete(b);
		return 1;
	}

	if (action == ACTION_STOP) {
		if (backend_is_available(b)) {
			b->action = action;
			is_actionated = 1;
		}
		backend_set_state(b, VALUE_STATE_DOWN);

		return is_actionated;
	}

	if (action == ACTION_START) {
		if (!backend_is_available(b)) {
			b->action = action;
			is_actionated = 1;
		}
		backend_set_state(b, VALUE_STATE_UP);
		return is_actionated;
	}

	if (b->action > action) {
		b->action = action;
		return 1;
	}

	return is_actionated;
}

int backend_s_set_action(struct farm *f, int action)
{
	struct backend *b, *next;

	list_for_each_entry_safe(b, next, &f->backends, list)
		backend_set_action(b, action);

	return 0;
}

int backend_s_delete(struct farm *f)
{
	struct backend *b, *next;

	list_for_each_entry_safe(b, next, &f->backends, list)
		backend_delete_node(b);

	f->total_bcks = 0;
	f->bcks_available = 0;
	f->bcks_usable = 0;
	f->total_weight = 0;
	f->bcks_have_if = 0;

	return 0;
}

int backend_s_validate(struct farm *f)
{
	struct backend *b, *next;
	int valid = 0;

	list_for_each_entry_safe(b, next, &f->backends, list) {
		valid = backend_validate(b);
		if (b->state == VALUE_STATE_CONFERR && valid)
			backend_set_state(b, VALUE_STATE_UP);
	}

	return 0;
}

int backend_set_attribute(struct config_pair *c)
{
	struct farm *f = obj_get_current_farm();
	struct backend *b = obj_get_current_backend();

	if (!f || (c->key != KEY_NAME && !b))
		return PARSER_OBJ_UNKNOWN;

	switch (c->key) {
	case KEY_NAME:
		b = backend_lookup_by_key(f, KEY_NAME, c->str_value, 0);
		if (!b) {
			b = backend_create(f, c->str_value);
			if (!b)
				return -1;
		}
		obj_set_current_backend(b);
		break;
	case KEY_NEWNAME:
		free(b->name);
		obj_set_attribute_string(c->str_value, &b->name);
		break;
	case KEY_FQDN:
		if (strcmp(b->fqdn, DEFAULT_FQDN) != 0)
			free(b->fqdn);
		obj_set_attribute_string(c->str_value, &b->fqdn);
		break;
	case KEY_IPADDR:
		backend_set_ipaddr(b, c->str_value);
		break;
	case KEY_ETHADDR:
		if (b->ethaddr)
			free(b->ethaddr);
		obj_set_attribute_string(c->str_value, &b->ethaddr);
		break;
	case KEY_PORT:
		backend_set_port(b, c->str_value);
		break;
	case KEY_SRCADDR:
		backend_set_srcaddr(b, c->str_value);
		break;
	case KEY_WEIGHT:
		backend_set_weight(b, c->int_value);
		break;
	case KEY_PRIORITY:
		backend_set_priority(b, c->int_value);
		break;
	case KEY_MARK:
		backend_set_mark(b, c->int_value);
		break;
	case KEY_STATE:
		if (c->int_value == VALUE_STATE_CONFERR)
			backend_set_state(b, VALUE_STATE_UP);
		else
			backend_set_state(b, c->int_value);
		break;
	case KEY_ESTCONNLIMIT:
		backend_set_estconnlimit(b, c->int_value);
		break;
	case KEY_ACTION:
		backend_set_action(b, c->int_value);
		break;
	case KEY_ESTCONNLIMIT_LOGPREFIX:
		if (strcmp(b->estconnlimit_logprefix, DEFAULT_B_ESTCONNLIMIT_LOGPREFIX) != 0)
			free(b->estconnlimit_logprefix);
		obj_set_attribute_string(c->str_value, &b->estconnlimit_logprefix);
		break;
	default:
		return -1;
	}

	return PARSER_OK;
}

int backend_set_state(struct backend *b, int new_value)
{
	int old_value = b->state;
	struct farm *f = b->parent;

	tools_printlog(LOG_DEBUG, "%s():%d: backend %s current value is %s, but new value will be %s",
				   __FUNCTION__, __LINE__, b->name, obj_print_state(old_value), obj_print_state(new_value));

	if (new_value == VALUE_STATE_UP) {
		if (!backend_validate(b)) {
			new_value = VALUE_STATE_CONFERR;
		}

		if (!backend_below_prio(b))
			new_value = VALUE_STATE_AVAIL;
	}

	if (old_value == new_value)
		return 0;

	b->state = new_value;

	switch (new_value) {
	case VALUE_STATE_CONFERR:
	case VALUE_STATE_OFF:
		if (old_value == VALUE_STATE_UP)
			b->action = ACTION_STOP;
		break;
	case VALUE_STATE_AVAIL:
		if (old_value == VALUE_STATE_UP)
			b->action = ACTION_STOP;
		return 0;
	case VALUE_STATE_UP:
		if (f->persistence != VALUE_META_NONE)
			session_backend_action(f, b, ACTION_START);
		if (old_value == VALUE_STATE_OFF)
			b->action = ACTION_RELOAD;
		else
			b->action = ACTION_START;
		break;
	case VALUE_STATE_DOWN:
		if (old_value == VALUE_STATE_UP || old_value == VALUE_STATE_OFF)
			b->action = ACTION_STOP;
		break;
	default:
		break;
	}

	if (b->action != ACTION_NONE) {
		farm_set_action(f, ACTION_RELOAD);
		backend_s_gen_priority(f, ACTION_NONE);
	}

	return 0;
}

int backend_s_set_ether_by_ipaddr(struct farm *f, const char *ip_bck, char *ether_bck)
{
	struct backend *b;
	int changed = 0;

	list_for_each_entry(b, &f->backends, list) {

		if (strcmp(b->ipaddr, ip_bck) != 0)
			continue;

		tools_printlog(LOG_DEBUG, "%s():%d: backend with ip address %s found", __FUNCTION__, __LINE__, ip_bck);

		if (!b->ethaddr || (b->ethaddr && strcmp(b->ethaddr, ether_bck) != 0)) {
			if (f->persistence != VALUE_META_NONE)
				session_get_timed(f);
			if (b->ethaddr)
				free(b->ethaddr);
			obj_set_attribute_string(ether_bck, &b->ethaddr);
			changed = 1;
			if (f->persistence != VALUE_META_NONE) {
				session_backend_action(f, b, ACTION_RELOAD);
				farm_set_action(f, ACTION_RELOAD);
				obj_rulerize(OBJ_START);
				session_s_delete(f, SESSION_TYPE_TIMED);
			}

			tools_printlog(LOG_INFO, "%s():%d: ether address changed for backend %s with %s", __FUNCTION__, __LINE__, b->name, ether_bck);
		}
	}

	return changed;
}

static void backend_set_netinfo(struct backend *b)
{
	if (backend_set_ifinfo(b) == 0 && backend_set_ipaddr_from_ether(b) == 0) {
		if (b->state == VALUE_STATE_CONFERR)
			backend_set_state(b, VALUE_STATE_UP);
	} else
		backend_set_state(b, VALUE_STATE_CONFERR);
}

int backend_s_set_netinfo(struct farm *f)
{
	struct backend *b;
	int changed = 0;

	tools_printlog(LOG_DEBUG, "%s():%d: finding backends for %s", __FUNCTION__, __LINE__, f->name);

	list_for_each_entry(b, &f->backends, list) {
		if (backend_validate(b))
			continue;
		backend_set_netinfo(b);
	}

	return changed;
}

struct backend * backend_get_first(struct farm *f)
{
	if (list_empty(&f->backends))
		return NULL;

	return list_first_entry(&f->backends, struct backend, list);
}

int bck_pre_actionable(struct config_pair *c)
{
	struct farm *f = obj_get_current_farm();
	struct backend *b = obj_get_current_backend();

	if (!f || !b)
		return -1;

	tools_printlog(LOG_DEBUG, "%s():%d: pre actionable backend %s of farm %s with param %d", __FUNCTION__, __LINE__, b->name, f->name, c->key);

	// changing priority of a down backend could affect others, force a farm restart
	if (b->state != VALUE_STATE_UP && b->state != VALUE_STATE_CONFERR && c->key == KEY_PRIORITY) {
		farm_set_action(f, ACTION_STOP);
		farmaddress_s_set_action(f, ACTION_STOP);
		farm_rulerize(f);
		return ACTION_FLUSH;
	}

	if (b->state != VALUE_STATE_UP && c->key != KEY_STATE)
		return ACTION_NONE;

	switch (c->key) {
	case KEY_NAME:
		break;

	case KEY_ETHADDR:
	case KEY_IPADDR:
	case KEY_SRCADDR:
	case KEY_MARK:
	case KEY_PRIORITY:
	case KEY_ESTCONNLIMIT:
		if (backend_set_action(b, ACTION_STOP)) {
			farm_set_action(f, ACTION_RELOAD);
			farmaddress_s_set_action(f, ACTION_RELOAD);
			farm_rulerize(f);
		}
		return ACTION_START;
		break;

	case KEY_PORT:
	case KEY_STATE:
	case KEY_WEIGHT:
	case KEY_ESTCONNLIMIT_LOGPREFIX:
		return ACTION_RELOAD;
		break;

	default:
		break;
	}

	return ACTION_NONE;
}

int bck_pos_actionable(struct config_pair *c, int action)
{
	struct farm *f = obj_get_current_farm();
	struct backend *b = obj_get_current_backend();

	if (!f || !b)
		return -1;

	tools_printlog(LOG_DEBUG, "%s():%d: pos actionable backend %s of farm %s with param %d action %d", __FUNCTION__, __LINE__, b->name, f->name, c->key, action);

	switch (action) {
	case ACTION_START:
		if (backend_set_action(b, ACTION_START)) {
			farm_set_action(f, ACTION_RELOAD);
			farmaddress_s_set_action(f, ACTION_RELOAD);
			farm_rulerize(f);
		}
		break;
	case ACTION_RELOAD:
		farm_set_action(f, ACTION_RELOAD);
		break;
	case ACTION_FLUSH:
		farm_set_action(f, ACTION_START);
		farm_rulerize(f);
		break;
	default:
		break;
	}

	return 0;
}

int backend_s_gen_priority(struct farm *f, int action)
{
	struct backend *b, *next;
	int are_down;
	int old_prio = f->priority;
	int new_prio = DEFAULT_PRIORITY;

	do {
		are_down = 0;
		list_for_each_entry_safe(b, next, &f->backends, list)
			if (b->priority == new_prio && b->state != VALUE_STATE_UP && b->state != VALUE_STATE_AVAIL)
				are_down++;
		new_prio += are_down;
	} while (are_down);

	f->priority = new_prio;

	list_for_each_entry_safe(b, next, &f->backends, list) {

		if (b->state == VALUE_STATE_UP || b->state == VALUE_STATE_AVAIL)
			backend_set_state(b, VALUE_STATE_UP);
	}

	backend_s_update_counters(f);
	return f->priority != old_prio;
}

int backend_get_mark(struct backend *b)
{
	int mark = b->mark;

	if (b->srcaddr && strcmp(b->srcaddr, "") != 0)
		mark |= b->parent->mark;
	else
		mark |= farm_get_mark(b->parent);

	return mark;
}

int backend_s_check_have_iface(struct farm *f)
{
	struct backend *b, *next;

	list_for_each_entry_safe(b, next, &f->backends, list) {
		if (b->ofidx == DEFAULT_IFIDX)
			continue;
		if (f->ofidx != b->ofidx)
			return 1;
	}

	return 0;
}
