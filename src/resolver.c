/*
 *
 *  Connection Manager
 *
 *  Copyright (C) 2007-2012  Intel Corporation. All rights reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#define _GNU_SOURCE
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <resolv.h>

#include "connman.h"

#define RESOLVER_FLAG_PUBLIC (1 << 0)

struct entry_data {
	char *interface;
	char *domain;
	char *server;
	unsigned int flags;
	guint timeout;
};

static GSList *entry_list = NULL;
static connman_bool_t dnsproxy_enabled = FALSE;

struct resolvfile_entry {
	char *interface;
	char *domain;
	char *server;
};

static GList *resolvfile_list = NULL;

static void resolvfile_remove_entries(GList *entries)
{
	GList *list;

	for (list = entries; list; list = list->next) {
		struct resolvfile_entry *entry = list->data;

		resolvfile_list = g_list_remove(resolvfile_list, entry);

		g_free(entry->server);
		g_free(entry->domain);
		g_free(entry->interface);
		g_free(entry);
	}

	g_list_free(entries);
}

static int resolvfile_export(void)
{
	GList *list;
	GString *content;
	int fd, err;
	unsigned int count;
	mode_t old_umask;

	content = g_string_new("# Generated by Connection Manager\n");

	/*
	 * Domains and nameservers are added in reverse so that the most
	 * recently appended entry is the primary one. No more than
	 * MAXDNSRCH/MAXNS entries are used.
	 */

	for (count = 0, list = g_list_last(resolvfile_list);
						list && (count < MAXDNSRCH);
						list = g_list_previous(list)) {
		struct resolvfile_entry *entry = list->data;

		if (!entry->domain)
			continue;

		if (count == 0)
			g_string_append_printf(content, "search ");

		g_string_append_printf(content, "%s ", entry->domain);
		count++;
	}

	if (count)
		g_string_append_printf(content, "\n");

	for (count = 0, list = g_list_last(resolvfile_list);
						list && (count < MAXNS);
						list = g_list_previous(list)) {
		struct resolvfile_entry *entry = list->data;

		if (!entry->server)
			continue;

		g_string_append_printf(content, "nameserver %s\n",
								entry->server);
		count++;
	}

	old_umask = umask(022);

	fd = open("/etc/resolv.conf", O_RDWR | O_CREAT | O_CLOEXEC,
					S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	if (fd < 0) {
		err = -errno;
		goto done;
	}

	if (ftruncate(fd, 0) < 0) {
		err = -errno;
		goto failed;
	}

	err = 0;

	if (write(fd, content->str, content->len) < 0)
		err = -errno;

failed:
	close(fd);

done:
	g_string_free(content, TRUE);
	umask(old_umask);

	return err;
}

int __connman_resolvfile_append(const char *interface, const char *domain,
							const char *server)
{
	struct resolvfile_entry *entry;

	DBG("interface %s server %s", interface, server);

	if (interface == NULL)
		return -ENOENT;

	entry = g_try_new0(struct resolvfile_entry, 1);
	if (entry == NULL)
		return -ENOMEM;

	entry->interface = g_strdup(interface);
	entry->domain = g_strdup(domain);
	entry->server = g_strdup(server);

	resolvfile_list = g_list_append(resolvfile_list, entry);

	return resolvfile_export();
}

int __connman_resolvfile_remove(const char *interface, const char *domain,
							const char *server)
{
	GList *list, *matches = NULL;

	DBG("interface %s server %s", interface, server);

	for (list = resolvfile_list; list; list = g_list_next(list)) {
		struct resolvfile_entry *entry = list->data;

		if (interface != NULL &&
				g_strcmp0(entry->interface, interface) != 0)
			continue;

		if (domain != NULL && g_strcmp0(entry->domain, domain) != 0)
			continue;

		if (g_strcmp0(entry->server, server) != 0)
			continue;

		matches = g_list_append(matches, entry);
	}

	resolvfile_remove_entries(matches);

	return resolvfile_export();
}

static void remove_entries(GSList *entries)
{
	GSList *list;

	for (list = entries; list; list = list->next) {
		struct entry_data *entry = list->data;

		entry_list = g_slist_remove(entry_list, entry);

		if (dnsproxy_enabled == TRUE) {
			__connman_dnsproxy_remove(entry->interface, entry->domain,
							entry->server);
		} else {
			__connman_resolvfile_remove(entry->interface, entry->domain,
							entry->server);
		}

		if (entry->timeout)
			g_source_remove(entry->timeout);
		g_free(entry->server);
		g_free(entry->domain);
		g_free(entry->interface);
		g_free(entry);
	}

	g_slist_free(entries);
}

static gboolean resolver_expire_cb(gpointer user_data)
{
	struct entry_data *entry = user_data;
	GSList *list;
	int index;

	DBG("interface %s domain %s server %s",
			entry->interface, entry->domain, entry->server);

	list = g_slist_append(NULL, entry);

	index = connman_inet_ifindex(entry->interface);
	if (index >= 0) {
		struct connman_service *service;
		service = __connman_service_lookup_from_index(index);
		if (service != NULL)
			__connman_service_nameserver_remove(service,
							entry->server, TRUE);
	}

	remove_entries(list);

	return FALSE;
}

static int append_resolver(const char *interface, const char *domain,
				const char *server, unsigned int lifetime,
							unsigned int flags)
{
	struct entry_data *entry;

	DBG("interface %s domain %s server %s lifetime %d flags %d",
				interface, domain, server, lifetime, flags);

	if (server == NULL && domain == NULL)
		return -EINVAL;

	entry = g_try_new0(struct entry_data, 1);
	if (entry == NULL)
		return -ENOMEM;

	entry->interface = g_strdup(interface);
	entry->domain = g_strdup(domain);
	entry->server = g_strdup(server);
	entry->flags = flags;
	if (lifetime) {
		int index;
		entry->timeout = g_timeout_add_seconds(lifetime,
						resolver_expire_cb, entry);

		/*
		 * We update the service only for those nameservers
		 * that are automagically added via netlink (lifetime > 0)
		 */
		index = connman_inet_ifindex(interface);
		if (index >= 0) {
			struct connman_service *service;
			service = __connman_service_lookup_from_index(index);
			if (service != NULL)
				__connman_service_nameserver_append(service,
								server, TRUE);
		}
	}
	entry_list = g_slist_append(entry_list, entry);

	if (dnsproxy_enabled == TRUE)
		__connman_dnsproxy_append(interface, domain, server);
	else
		__connman_resolvfile_append(interface, domain, server);

	return 0;
}

/**
 * connman_resolver_append:
 * @interface: network interface
 * @domain: domain limitation
 * @server: server address
 *
 * Append resolver server address to current list
 */
int connman_resolver_append(const char *interface, const char *domain,
						const char *server)
{
	GSList *list, *matches = NULL;

	DBG("interface %s domain %s server %s", interface, domain, server);

	if (server == NULL && domain == NULL)
		return -EINVAL;

	for (list = entry_list; list; list = list->next) {
		struct entry_data *entry = list->data;

		if (entry->timeout > 0 ||
				g_strcmp0(entry->interface, interface) != 0 ||
				g_strcmp0(entry->domain, domain) != 0 ||
				g_strcmp0(entry->server, server) != 0)
			continue;

		matches = g_slist_append(matches, entry);
	}

	if (matches != NULL)
		remove_entries(matches);

	return append_resolver(interface, domain, server, 0, 0);
}

/**
 * connman_resolver_append_lifetime:
 * @interface: network interface
 * @domain: domain limitation
 * @server: server address
 * @timeout: server lifetime in seconds
 *
 * Append resolver server address to current list
 */
int connman_resolver_append_lifetime(const char *interface, const char *domain,
				const char *server, unsigned int lifetime)
{
	GSList *list;

	DBG("interface %s domain %s server %s lifetime %d",
				interface, domain, server, lifetime);

	if (server == NULL)
		return -EINVAL;

	for (list = entry_list; list; list = list->next) {
		struct entry_data *entry = list->data;

		if (!entry->timeout ||
				g_strcmp0(entry->interface, interface) ||
				g_strcmp0(entry->domain, domain) ||
				g_strcmp0(entry->server, server))
			continue;

		g_source_remove(entry->timeout);

		if (lifetime == 0) {
			resolver_expire_cb(entry);
			return 0;
		}

		entry->timeout = g_timeout_add_seconds(lifetime,
						resolver_expire_cb, entry);
		return 0;
	}

	return append_resolver(interface, domain, server, lifetime, 0);
}

/**
 * connman_resolver_remove:
 * @interface: network interface
 * @domain: domain limitation
 * @server: server address
 *
 * Remover resolver server address from current list
 */
int connman_resolver_remove(const char *interface, const char *domain,
							const char *server)
{
	GSList *list, *matches = NULL;

	DBG("interface %s domain %s server %s", interface, domain, server);

	if (server == NULL)
		return -EINVAL;

	for (list = entry_list; list; list = list->next) {
		struct entry_data *entry = list->data;

		if (interface != NULL &&
				g_strcmp0(entry->interface, interface) != 0)
			continue;

		if (domain != NULL && g_strcmp0(entry->domain, domain) != 0)
			continue;

		if (g_strcmp0(entry->server, server) != 0)
			continue;

		matches = g_slist_append(matches, entry);
	}

	if (matches == NULL)
		return -ENOENT;

	remove_entries(matches);

	return 0;
}

/**
 * connman_resolver_remove_all:
 * @interface: network interface
 *
 * Remove all resolver server address for the specified interface
 */
int connman_resolver_remove_all(const char *interface)
{
	GSList *list, *matches = NULL;

	DBG("interface %s", interface);

	if (interface == NULL)
		return -EINVAL;

	for (list = entry_list; list; list = list->next) {
		struct entry_data *entry = list->data;

		if (g_strcmp0(entry->interface, interface) != 0)
			continue;

		matches = g_slist_append(matches, entry);
	}

	if (matches == NULL)
		return -ENOENT;

	remove_entries(matches);

	return 0;
}

/**
 * connman_resolver_flush:
 *
 * Flush pending resolver requests
 */
void connman_resolver_flush(void)
{
	if (dnsproxy_enabled == TRUE)
		__connman_dnsproxy_flush();

	return;
}

int __connman_resolver_redo_servers(const char *interface)
{
	GSList *list;

	if (dnsproxy_enabled == FALSE)
		return 0;

	DBG("interface %s", interface);

	if (interface == NULL)
		return -EINVAL;

	for (list = entry_list; list; list = list->next) {
		struct entry_data *entry = list->data;

		if (entry->timeout == 0 ||
				g_strcmp0(entry->interface, interface) != 0)
			continue;

		/*
		 * We remove the server, and then re-create so that it will
		 * use proper source addresses when sending DNS queries.
		 */
		__connman_dnsproxy_remove(entry->interface, entry->domain,
					entry->server);

		__connman_dnsproxy_append(entry->interface, entry->domain,
					entry->server);
	}

	return 0;
}

static void free_entry(gpointer data)
{
	struct entry_data *entry = data;
	g_free(entry->interface);
	g_free(entry->domain);
	g_free(entry->server);
	g_free(entry);
}

static void free_resolvfile(gpointer data)
{
	struct resolvfile_entry *entry = data;
	g_free(entry->interface);
	g_free(entry->domain);
	g_free(entry->server);
	g_free(entry);
}

int __connman_resolver_init(connman_bool_t dnsproxy)
{
	int i;
	char **ns;

	DBG("dnsproxy %d", dnsproxy);

	if (dnsproxy == FALSE)
		return 0;

	if (__connman_dnsproxy_init() < 0) {
		/* Fall back to resolv.conf */
		return 0;
	}

	dnsproxy_enabled = TRUE;

	ns = connman_setting_get_string_list("FallbackNameservers");
	for (i = 0; ns != NULL && ns[i] != NULL; i += 1) {
		DBG("server %s", ns[i]);
		append_resolver(NULL, NULL, ns[i], 0, RESOLVER_FLAG_PUBLIC);
	}

	return 0;
}

void __connman_resolver_cleanup(void)
{
	DBG("");

	if (dnsproxy_enabled == TRUE)
		__connman_dnsproxy_cleanup();
	else {
		GList *list;
		GSList *slist;

		for (list = resolvfile_list; list; list = g_list_next(list))
			free_resolvfile(list->data);
		g_list_free(resolvfile_list);
		resolvfile_list = NULL;

		for (slist = entry_list; slist; slist = g_slist_next(slist))
			free_entry(slist->data);
		g_slist_free(entry_list);
		entry_list = NULL;
	}
}
