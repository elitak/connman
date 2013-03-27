/*
 *
 *  Connection Manager
 *
 *  Copyright (C) 2012-2013  Intel Corporation. All rights reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <stdbool.h>

#include <glib.h>
#include <gdbus.h>

#include "dbus_helpers.h"
#include "input.h"
#include "services.h"
#include "commands.h"

static DBusConnection *connection;

static char *ipv4[] = {
	"Method",
	"Address",
	"Netmask",
	"Gateway",
	NULL
};

static char *ipv6[] = {
	"Method",
	"Address",
	"PrefixLength",
	"Gateway",
	NULL
};

static int cmd_help(char *args[], int num, struct option *options);

static int parse_boolean(char *arg)
{
	if (arg == NULL)
		return -1;

	if (strcasecmp(arg, "no") == 0 ||
			strcasecmp(arg, "false") == 0 ||
			strcasecmp(arg, "off" ) == 0 ||
			strcasecmp(arg, "disable" ) == 0 ||
			strcasecmp(arg, "n") == 0 ||
			strcasecmp(arg, "f") == 0 ||
			strcasecmp(arg, "0") == 0)
		return 0;

	if (strcasecmp(arg, "yes") == 0 ||
			strcasecmp(arg, "true") == 0 ||
			strcasecmp(arg, "on") == 0 ||
			strcasecmp(arg, "enable" ) == 0 ||
			strcasecmp(arg, "y") == 0 ||
			strcasecmp(arg, "t") == 0 ||
			strcasecmp(arg, "1") == 0)
		return 1;

	return -1;
}

static int parse_args(char *arg, struct option *options)
{
	int i;

	if (arg == NULL)
		return -1;

	for (i = 0; options[i].name != NULL; i++) {
		if (strcmp(options[i].name, arg) == 0 ||
				(strncmp(arg, "--", 2) == 0 &&
					strcmp(&arg[2], options[i].name) == 0))
			return options[i].val;
	}

	return '?';
}

static void enable_return(DBusMessageIter *iter, const char *error,
		void *user_data)
{
	char *tech = user_data;
	char *str;

	str = strrchr(tech, '/');
	if (str != NULL)
		str++;
	else
		str = tech;

	if (error == NULL) {
		fprintf(stdout, "Enabled %s\n", str);
	} else
		fprintf(stderr, "Error %s: %s\n", str, error);

	g_free(user_data);
}

static int cmd_enable(char *args[], int num, struct option *options)
{
	char *tech;
	dbus_bool_t b = TRUE;

	if (num > 2)
		return -E2BIG;

	if (num < 2)
		return -EINVAL;

	if (strcmp(args[1], "offlinemode") == 0) {
		tech = g_strdup(args[1]);
		return __connmanctl_dbus_set_property(connection, "/",
				"net.connman.Manager", enable_return, tech,
				"OfflineMode", DBUS_TYPE_BOOLEAN, &b);
	}

	tech = g_strdup_printf("/net/connman/technology/%s", args[1]);
	return __connmanctl_dbus_set_property(connection, tech,
				"net.connman.Technology", enable_return, tech,
				"Powered", DBUS_TYPE_BOOLEAN, &b);
}

static void disable_return(DBusMessageIter *iter, const char *error,
		void *user_data)
{
	char *tech = user_data;
	char *str;

	str = strrchr(tech, '/');
	if (str != NULL)
		str++;
	else
		str = tech;

	if (error == NULL) {
		fprintf(stdout, "Disabled %s\n", str);
	} else
		fprintf(stderr, "Error %s: %s\n", str, error);

	g_free(user_data);
}

static int cmd_disable(char *args[], int num, struct option *options)
{
	char *tech;
	dbus_bool_t b = FALSE;

	if (num > 2)
		return -E2BIG;

	if (num < 2)
		return -EINVAL;

	if (strcmp(args[1], "offlinemode") == 0) {
		tech = g_strdup(args[1]);
		return __connmanctl_dbus_set_property(connection, "/",
				"net.connman.Manager", disable_return, tech,
				"OfflineMode", DBUS_TYPE_BOOLEAN, &b);
	}

	tech = g_strdup_printf("/net/connman/technology/%s", args[1]);
	return __connmanctl_dbus_set_property(connection, tech,
				"net.connman.Technology", disable_return, tech,
				"Powered", DBUS_TYPE_BOOLEAN, &b);
}

static void state_print(DBusMessageIter *iter, const char *error,
		void *user_data)
{
	DBusMessageIter entry;

	if (error != NULL) {
		fprintf(stderr, "Error: %s", error);
		return;
	}

	dbus_message_iter_recurse(iter, &entry);
	__connmanctl_dbus_print(&entry, "  ", " = ", "\n");
	fprintf(stdout, "\n");
}

static int cmd_state(char *args[], int num, struct option *options)
{
	if (num > 1)
		return -E2BIG;

	return __connmanctl_dbus_method_call(connection, "/",
			"net.connman.Manager", "GetProperties",
			state_print, NULL, DBUS_TYPE_INVALID);
}

static void services_list(DBusMessageIter *iter, const char *error,
		void *user_data)
{
	if (error == NULL) {
		__connmanctl_services_list(iter);
		fprintf(stdout, "\n");
	} else {
		fprintf(stderr, "Error: %s\n", error);
	}
}

static void services_properties(DBusMessageIter *iter, const char *error,
		void *user_data)
{
	char *path = user_data;
	char *str;
	DBusMessageIter dict;

	if (error == NULL) {
		fprintf(stdout, "%s\n", path);

		dbus_message_iter_recurse(iter, &dict);
		__connmanctl_dbus_print(&dict, "  ", " = ", "\n");

		fprintf(stdout, "\n");

	} else {
		str = strrchr(path, '/');
		if (str != NULL)
			str++;
		else
			str = path;

		fprintf(stderr, "Error %s: %s\n", str, error);
	}

	g_free(user_data);
}

static int cmd_services(char *args[], int num, struct option *options)
{
	char *service_name = NULL;
	char *path;
	int c;

	if (num > 3)
		return -E2BIG;

	c = parse_args(args[1], options);
	switch (c) {
	case -1:
		break;
	case 'p':
		if (num < 3)
			return -EINVAL;
		service_name = args[2];
		break;
	default:
		if (num > 2)
			return -E2BIG;
		service_name = args[1];
		break;
	}

	if (service_name == NULL) {
		return __connmanctl_dbus_method_call(connection, "/",
			"net.connman.Manager", "GetServices",
			services_list, NULL, DBUS_TYPE_INVALID);
	}

	path = g_strdup_printf("/net/connman/service/%s", service_name);
	return __connmanctl_dbus_method_call(connection, path,
			"net.connman.Service", "GetProperties",
			services_properties, path, DBUS_TYPE_INVALID);
}

static void technology_print(DBusMessageIter *iter, const char *error,
		void *user_data)
{
	DBusMessageIter array;

	if (error != NULL) {
		fprintf(stderr, "Error: %s\n", error);
		return;
	}

	dbus_message_iter_recurse(iter, &array);
	while (dbus_message_iter_get_arg_type(&array) == DBUS_TYPE_STRUCT) {
		DBusMessageIter entry, dict;
		const char *path;

		dbus_message_iter_recurse(&array, &entry);
		dbus_message_iter_get_basic(&entry, &path);
		fprintf(stdout, "%s\n", path);

		dbus_message_iter_next(&entry);

		dbus_message_iter_recurse(&entry, &dict);
		__connmanctl_dbus_print(&dict, "  ", " = ", "\n");
		fprintf(stdout, "\n");

		dbus_message_iter_next(&array);
	}
}

static int cmd_technologies(char *args[], int num, struct option *options)
{
	if (num > 1)
		return -E2BIG;

	return __connmanctl_dbus_method_call(connection, "/",
			"net.connman.Manager", "GetTechnologies",
			technology_print, NULL,	DBUS_TYPE_INVALID);
}

static void scan_return(DBusMessageIter *iter, const char *error,
		void *user_data)
{
	char *path = user_data;

	if (error == NULL) {
		char *str = strrchr(path, '/');
		str++;
		fprintf(stdout, "Scan completed for %s\n", str);
	} else
		fprintf(stderr, "Error %s: %s", path, error);

	g_free(user_data);
}

static int cmd_scan(char *args[], int num, struct option *options)
{
	char *path;

	if (num > 2)
		return -E2BIG;

	if (num < 2)
		return -EINVAL;

	path = g_strdup_printf("/net/connman/technology/%s", args[1]);
	return __connmanctl_dbus_method_call(connection, path,
			"net.connman.Technology", "Scan",
			scan_return, path, DBUS_TYPE_INVALID);
}

static void connect_return(DBusMessageIter *iter, const char *error,
		void *user_data)
{
	char *path = user_data;

	if (error == NULL) {
		char *str = strrchr(path, '/');
		str++;
		fprintf(stdout, "Connected %s\n", str);
	} else
		fprintf(stderr, "Error %s: %s\n", path, error);

	g_free(user_data);
}

static int cmd_connect(char *args[], int num, struct option *options)
{
	char *path;

	if (num > 2)
		return -E2BIG;

	if (num < 2)
		return -EINVAL;

	path = g_strdup_printf("/net/connman/service/%s", args[1]);
	return __connmanctl_dbus_method_call(connection, path,
			"net.connman.Service", "Connect",
			connect_return, path, DBUS_TYPE_INVALID);
}

static void disconnect_return(DBusMessageIter *iter, const char *error,
		void *user_data)
{
	char *path = user_data;

	if (error == NULL) {
		char *str = strrchr(path, '/');
		str++;
		fprintf(stdout, "Disconnected %s\n", str);
	} else
		fprintf(stderr, "Error %s: %s\n", path, error);

	g_free(user_data);
}

static int cmd_disconnect(char *args[], int num, struct option *options)
{
	char *path;

	if (num > 2)
		return -E2BIG;

	if (num < 2)
		return -EINVAL;

	path = g_strdup_printf("/net/connman/service/%s", args[1]);
	return __connmanctl_dbus_method_call(connection, path,
			"net.connman.Service", "Disconnect",
			disconnect_return, path, DBUS_TYPE_INVALID);

	return 0;
}

static void config_return(DBusMessageIter *iter, const char *error,
		void *user_data)
{
	char *service_name = user_data;

	if (error != NULL)
		fprintf(stderr, "Error %s: %s\n", service_name, error);

	g_free(user_data);
}

struct config_append {
	char **opts;
	int values;
};

static void config_append_ipv4(DBusMessageIter *iter,
		void *user_data)
{
	struct config_append *append = user_data;
	char **opts = append->opts;
	int i = 0;

	if (opts == NULL)
		return;

	while (opts[i] != NULL && ipv4[i] != NULL) {
		__connmanctl_dbus_append_dict_entry(iter, ipv4[i],
				DBUS_TYPE_STRING, &opts[i]);
		i++;
	}

	append->values = i;
}

static void config_append_ipv6(DBusMessageIter *iter, void *user_data)
{
	struct config_append *append = user_data;
	char **opts = append->opts;

	if (opts == NULL)
		return;

	append->values = 1;

	if (g_strcmp0(opts[0], "auto") == 0) {
		char *str;

		switch (parse_boolean(opts[1])) {
		case 0:
			append->values = 2;

			str = "disabled";
			__connmanctl_dbus_append_dict_entry(iter, "Privacy",
					DBUS_TYPE_STRING, &str);
			break;

		case 1:
			append->values = 2;

			str = "enabled";
			__connmanctl_dbus_append_dict_entry(iter, "Privacy",
					DBUS_TYPE_STRING, &str);
			break;

		default:
			if (opts[1] != NULL) {
				append->values = 2;

				if (g_strcmp0(opts[0], "prefered") != 0) {
					fprintf(stderr, "Error %s: %s\n",
							opts[1],
							strerror(-EINVAL));
					return;
				}

				str = "prefered";
				__connmanctl_dbus_append_dict_entry(iter,
						"Privacy", DBUS_TYPE_STRING,
						&str);
			}
			break;
		}
	} else if (g_strcmp0(opts[0], "manual") == 0) {
		int i = 1;

		while (opts[i] != NULL && ipv6[i] != NULL) {
			if (i == 2) {
				int value = atoi(opts[i]);
				__connmanctl_dbus_append_dict_entry(iter,
						ipv6[i], DBUS_TYPE_BYTE,
						&value);
			} else {
				__connmanctl_dbus_append_dict_entry(iter,
						ipv6[i], DBUS_TYPE_STRING,
						&opts[i]);
			}
			i++;
		}

		append->values = i;

	} else if (g_strcmp0(opts[0], "off") != 0) {
		fprintf(stderr, "Error %s: %s\n", opts[0], strerror(-EINVAL));

		return;
	}

	__connmanctl_dbus_append_dict_entry(iter, "Method", DBUS_TYPE_STRING,
				&opts[0]);
}

static void config_append_str(DBusMessageIter *iter, void *user_data)
{
	struct config_append *append = user_data;
	char **opts = append->opts;
	int i = 0;

	if (opts == NULL)
		return;

	while (opts[i] != NULL) {
		dbus_message_iter_append_basic(iter, DBUS_TYPE_STRING,
				&opts[i]);
		i++;
	}

	append->values = i;
}

static void append_servers(DBusMessageIter *iter, void *user_data)
{
	struct config_append *append = user_data;
	char **opts = append->opts;
	int i = 1;

	if (opts == NULL)
		return;

	while (opts[i] != NULL && g_strcmp0(opts[i], "--excludes") != 0) {
		dbus_message_iter_append_basic(iter, DBUS_TYPE_STRING,
				&opts[i]);
		i++;
	}

	append->values = i;
}

static void append_excludes(DBusMessageIter *iter, void *user_data)
{
	struct config_append *append = user_data;
	char **opts = append->opts;
	int i = append->values;

	if (opts == NULL || opts[i] == NULL ||
			g_strcmp0(opts[i], "--excludes") != 0)
		return;

	i++;
	while (opts[i] != NULL) {
		dbus_message_iter_append_basic(iter, DBUS_TYPE_STRING,
				&opts[i]);
		i++;
	}

	append->values = i;
}

static void config_append_proxy(DBusMessageIter *iter, void *user_data)
{
	struct config_append *append = user_data;
	char **opts = append->opts;

	if (opts == NULL)
		return;

	if (g_strcmp0(opts[0], "manual") == 0) {
		__connmanctl_dbus_append_dict_string_array(iter, "Servers",
				append_servers, append);

		__connmanctl_dbus_append_dict_string_array(iter, "Excludes",
				append_excludes, append);

	} else if (g_strcmp0(opts[0], "auto") == 0) {
		if (opts[1] != NULL) {
			__connmanctl_dbus_append_dict_entry(iter, "URL",
					DBUS_TYPE_STRING, &opts[1]);
			append->values++;
		}

	} else if (g_strcmp0(opts[0], "direct") != 0)
		return;

	__connmanctl_dbus_append_dict_entry(iter, "Method",DBUS_TYPE_STRING,
			&opts[0]);

	append->values++;
}

static int cmd_config(char *args[], int num, struct option *options)
{
	int result = 0, res = 0, index = 2, oldindex = 0;
	int c;
	char *service_name, *path;
	char **opt_start;
	dbus_bool_t val;
	struct config_append append;

	service_name = args[1];
	if (service_name == NULL)
		return -EINVAL;

	while (index < num && args[index] != NULL) {
		c = parse_args(args[index], options);
		opt_start = &args[index + 1];
		append.opts = opt_start;
		append.values = 0;

		res = 0;

		oldindex = index;
		path = g_strdup_printf("/net/connman/service/%s", service_name);

		switch (c) {
		case 'a':
			switch (parse_boolean(*opt_start)) {
			case 1:
				val = TRUE;
				break;
			case 0:
				val = FALSE;
				break;
			default:
				res = -EINVAL;
				break;
			}

			index++;

			if (res == 0) {
				res = __connmanctl_dbus_set_property(connection,
						path, "net.connman.Service",
						config_return,
						g_strdup(service_name),
						"AutoConnect",
						DBUS_TYPE_BOOLEAN, &val);
			}
			break;
		case 'i':
			res = __connmanctl_dbus_set_property_dict(connection,
					path, "net.connman.Service",
					config_return, g_strdup(service_name),
					"IPv4.Configuration", DBUS_TYPE_STRING,
					config_append_ipv4, &append);
			index += append.values;
			break;

		case 'v':
			res = __connmanctl_dbus_set_property_dict(connection,
					path, "net.connman.Service",
					config_return, g_strdup(service_name),
					"IPv6.Configuration", DBUS_TYPE_STRING,
					config_append_ipv6, &append);
			index += append.values;
			break;

		case 'n':
			res = __connmanctl_dbus_set_property_array(connection,
					path, "net.connman.Service",
					config_return, g_strdup(service_name),
					"Nameservers.Configuration",
					DBUS_TYPE_STRING, config_append_str,
					&append);
			index += append.values;
			break;

		case 't':
			res = __connmanctl_dbus_set_property_array(connection,
					path, "net.connman.Service",
					config_return, g_strdup(service_name),
					"Timeservers.Configuration",
					DBUS_TYPE_STRING, config_append_str,
					&append);
			index += append.values;
			break;

		case 'd':
			res = __connmanctl_dbus_set_property_array(connection,
					path, "net.connman.Service",
					config_return, g_strdup(service_name),
					"Domains.Configuration",
					DBUS_TYPE_STRING, config_append_str,
					&append);
			index += append.values;
			break;

		case 'x':
			res = __connmanctl_dbus_set_property_dict(connection,
					path, "net.connman.Service",
					config_return, g_strdup(service_name),
					"Proxy.Configuration",
					DBUS_TYPE_STRING, config_append_proxy,
					&append);
			index += append.values;
			break;
		case 'r':
			res = __connmanctl_dbus_method_call(connection,
					path, "net.connman.Service", "Remove",
					config_return, g_strdup(service_name),
					DBUS_TYPE_INVALID);
			break;
		default:
			res = -EINVAL;
			break;
		}

		g_free(path);

		if (res < 0) {
			if (res == -EINPROGRESS)
				result = -EINPROGRESS;
			else
				printf("Error '%s': %s\n", args[oldindex],
						strerror(-res));
		} else
			index += res;

		index++;
	}

	return result;
}

static DBusHandlerResult monitor_changed(DBusConnection *connection,
		DBusMessage *message, void *user_data)
{
	DBusMessageIter iter;
	const char *interface, *path;

	interface = dbus_message_get_interface(message);
	if (strncmp(interface, "net.connman.", 12) != 0)
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

	interface = strrchr(interface, '.');
	if (interface != NULL && *interface != '\0')
		interface++;

	path = strrchr(dbus_message_get_path(message), '/');
	if (path != NULL && *path != '\0')
		path++;

	__connmanctl_save_rl();

	if (dbus_message_is_signal(message, "net.connman.Manager",
					"ServicesChanged") == TRUE) {

		fprintf(stdout, "%-12s %-20s = {\n", interface,
				"ServicesChanged");
		dbus_message_iter_init(message, &iter);
		__connmanctl_services_list(&iter);
		fprintf(stdout, "\n}\n");

		__connmanctl_redraw_rl();

		return DBUS_HANDLER_RESULT_HANDLED;
	}

	if (dbus_message_is_signal(message, "net.connman.Manager",
					"TechnologyAdded") == TRUE)
		path = "TechnologyAdded";

	if (dbus_message_is_signal(message, "net.connman.Manager",
					"TechnologyRemoved") == TRUE)
		path = "TechnologyRemoved";

	fprintf(stdout, "%-12s %-20s ", interface, path);
	dbus_message_iter_init(message, &iter);

	__connmanctl_dbus_print(&iter, "", " = ", " = ");
	fprintf(stdout, "\n");

	__connmanctl_redraw_rl();

	return DBUS_HANDLER_RESULT_HANDLED;
}

static bool monitor_s = false;
static bool monitor_t = false;
static bool monitor_m = false;

static void monitor_add(char *interface)
{
	char *rule;
	DBusError err;

	if (monitor_s == false && monitor_t == false && monitor_m == false)
		dbus_connection_add_filter(connection, monitor_changed,
				NULL, NULL);

	if (g_strcmp0(interface, "Service") == 0) {
		if (monitor_s == true)
			return;
		monitor_s = true;
	} else if (g_strcmp0(interface, "Technology") == 0) {
		if (monitor_t == true)
			return;
		monitor_t = true;
	} else if (g_strcmp0(interface, "Manager") == 0) {
		if (monitor_m == true)
			return;
		monitor_m = true;
	} else
		return;

	dbus_error_init(&err);
	rule  = g_strdup_printf("type='signal',interface='net.connman.%s'",
			interface);
	dbus_bus_add_match(connection, rule, &err);
	g_free(rule);

	if (dbus_error_is_set(&err))
		fprintf(stderr, "Error: %s\n", err.message);
}

static void monitor_del(char *interface)
{
	char *rule;

	if (g_strcmp0(interface, "Service") == 0) {
		if (monitor_s == false)
			return;
		monitor_s = false;
	} else if (g_strcmp0(interface, "Technology") == 0) {
		if (monitor_t == false)
			return;
		monitor_t = false;
	} else if (g_strcmp0(interface, "Manager") == 0) {
		if (monitor_m == false)
			return;
		monitor_m = false;
	} else
		return;

	rule  = g_strdup_printf("type='signal',interface='net.connman.%s'",
			interface);
	dbus_bus_remove_match(connection, rule, NULL);
	g_free(rule);

	if (monitor_s == false && monitor_t == false && monitor_m == false)
		dbus_connection_remove_filter(connection, monitor_changed,
				NULL);
}

static int cmd_monitor(char *args[], int num, struct option *options)
{
	bool add = true;
	int c;

	if (num > 3)
		return -E2BIG;

	if (num == 3) {
		switch (parse_boolean(args[2])) {
		case 0:
			add = false;
			break;

		default:
			break;
		}
	}

	c = parse_args(args[1], options);
	switch (c) {
	case -1:
		monitor_add("Service");
		monitor_add("Technology");
		monitor_add("Manager");
		break;

	case 's':
		if (add == true)
			monitor_add("Service");
		else
			monitor_del("Service");
		break;

	case 'c':
		if (add == true)
			monitor_add("Technology");
		else
			monitor_del("Technology");
		break;

	case 'm':
		if (add == true)
			monitor_add("Manager");
		else
			monitor_del("Manager");
		break;

	default:
		switch(parse_boolean(args[1])) {
		case 0:
			monitor_del("Service");
			monitor_del("Technology");
			monitor_del("Manager");
			break;

		case 1:
			monitor_add("Service");
			monitor_add("Technology");
			monitor_add("Manager");
			break;

		default:
			return -EINVAL;
		}
	}

	if (add == true)
		return -EINPROGRESS;

	return 0;
}

static int cmd_exit(char *args[], int num, struct option *options)
{
	return 1;
}

static struct option service_options[] = {
	{"properties", required_argument, 0, 'p'},
	{ NULL, }
};

static const char *service_desc[] = {
	"[<service>]      (obsolete)",
	NULL
};

static struct option config_options[] = {
	{"nameservers", required_argument, 0, 'n'},
	{"timeservers", required_argument, 0, 't'},
	{"domains", required_argument, 0, 'd'},
	{"ipv6", required_argument, 0, 'v'},
	{"proxy", required_argument, 0, 'x'},
	{"autoconnect", required_argument, 0, 'a'},
	{"ipv4", required_argument, 0, 'i'},
	{"remove", 0, 0, 'r'},
	{ NULL, }
};

static const char *config_desc[] = {
	"<dns1> [<dns2>] [<dns3>]",
	"<ntp1> [<ntp2>] [...]",
	"<domain1> [<domain2>] [...]",
	"off|auto [enable|disable|prefered]|\n"
	"\t\t\tmanual <address> <prefixlength> <gateway>",
	"direct|auto <URL>|manual <URL1> [<URL2>] [...]\n"
	"\t\t\t[exclude <exclude1> [<exclude2>] [...]]",
	"yes|no",
	"off|dhcp|manual <address> <prefixlength> <gateway>",
	"                 Remove service",
	NULL
};

static struct option monitor_options[] = {
	{"services", no_argument, 0, 's'},
	{"tech", no_argument, 0, 'c'},
	{"manager", no_argument, 0, 'm'},
	{ NULL, }
};

static const char *monitor_desc[] = {
	"[off]            Monitor only services",
	"[off]            Monitor only technologies",
	"[off]            Monitor only manager interface",
	NULL
};

static const struct {
        const char *cmd;
	const char *argument;
        struct option *options;
	const char **options_desc;
        int (*func) (char *args[], int num, struct option *options);
        const char *desc;
} cmd_table[] = {
	{ "enable",       "<technology>|offline", NULL,    NULL,
	  cmd_enable, "Enables given technology or offline mode" },
	{ "disable",      "<technology>|offline", NULL,    NULL,
	  cmd_disable, "Disables given technology or offline mode"},
	{ "state",        NULL,           NULL,            NULL,
	  cmd_state, "Shows if the system is online or offline" },
	{ "services",     "[<service>]",  service_options, &service_desc[0],
	  cmd_services, "Display services" },
	{ "technologies", NULL,           NULL,            NULL,
	  cmd_technologies, "Display technologies" },
	{ "scan",         "<technology>", NULL,            NULL,
	  cmd_scan, "Scans for new services for given technology" },
	{ "connect",      "<service>",    NULL,            NULL,
	  cmd_connect, "Connect a given service" },
	{ "disconnect",   "<service>",    NULL,            NULL,
	  cmd_disconnect, "Disconnect a given service" },
	{ "config",       "<service>",    config_options,  &config_desc[0],
	  cmd_config, "Set service configuration options" },
	{ "monitor",      "[off]",        monitor_options, &monitor_desc[0],
	  cmd_monitor, "Monitor signals from interfaces" },
	{ "help",         NULL,           NULL,            NULL,
	  cmd_help, "Show help" },
	{ "exit",         NULL,           NULL,            NULL,
	  cmd_exit,       "Exit" },
	{ "quit",         NULL,           NULL,            NULL,
	  cmd_exit,       "Quit" },
	{  NULL, },
};

static int cmd_help(char *args[], int num, struct option *options)
{
	bool interactive = __connmanctl_is_interactive();
	int i, j;

	if (interactive == false)
		fprintf(stdout, "Usage: connmanctl [[command] [args]]\n");

	for (i = 0; cmd_table[i].cmd != NULL; i++) {
		const char *cmd = cmd_table[i].cmd;
		const char *argument = cmd_table[i].argument;
		const char *desc = cmd_table[i].desc;

		printf("%-12s%-22s%s\n", cmd != NULL? cmd: "",
				argument != NULL? argument: "",
				desc != NULL? desc: "");

		if (cmd_table[i].options != NULL) {
			for (j = 0; cmd_table[i].options[j].name != NULL;
			     j++) {
				const char *options_desc =
					cmd_table[i].options_desc != NULL ?
					cmd_table[i].options_desc[j]: "";

				printf("   --%-12s%s\n",
						cmd_table[i].options[j].name,
						options_desc);
			}
		}
	}

	if (interactive == false)
		fprintf(stdout, "\nNote: arguments and output are considered "
				"EXPERIMENTAL for now.\n");

	return 0;
}

int commands(DBusConnection *dbus_conn, char *argv[], int argc)
{
	int i, result;

	connection = dbus_conn;

	for (i = 0; cmd_table[i].cmd != NULL; i++) {
		if (g_strcmp0(cmd_table[i].cmd, argv[0]) == 0 &&
				cmd_table[i].func != NULL) {
			result = cmd_table[i].func(argv, argc,
					cmd_table[i].options);
			if (result < 0 && result != -EINPROGRESS)
				fprintf(stderr, "Error '%s': %s\n", argv[0],
						strerror(-result));
			return result;
		}
	}

	fprintf(stderr, "Error '%s': Unknown command\n", argv[0]);
	return -EINVAL;
}
