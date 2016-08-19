#include <util/platform.h>
#include <util/dstr.h>
#include <obs-module.h>
#include <jansson.h>

static void fill_servers(obs_property_t *servers_prop, json_t *service, const char *name);

struct ftl_beam {
	char *server, *key;
};

static const char *ftl_beam_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("Beam FTL Service");
}

static void ftl_beam_update(void *data, obs_data_t *settings)
{
	struct ftl_beam *service = data;

	bfree(service->server);
	bfree(service->key);

	service->server = bstrdup(obs_data_get_string(settings, "server"));
	service->key    = bstrdup(obs_data_get_string(settings, "key"));
}

static void ftl_beam_destroy(void *data)
{
	struct ftl_beam *service = data;

	bfree(service->server);
	bfree(service->key);
	bfree(service);
}

static void *ftl_beam_create(obs_data_t *settings, obs_service_t *service)
{
	struct ftl_beam *data = bzalloc(sizeof(struct ftl_beam));
	ftl_beam_update(data, settings);

	UNUSED_PARAMETER(service);
	return data;
}

static json_t *open_json_file(const char *file)
{
	char         *file_data = os_quick_read_utf8_file(file);
	json_error_t error;
	json_t       *root;
	json_t       *list;
	int          format_ver;

	if (!file_data)
		return NULL;

	root = json_loads(file_data, JSON_REJECT_DUPLICATES, &error);
	bfree(file_data);

	if (!root) {
		blog(LOG_WARNING, "ftl-beam.c: [open_json_file] "
			"Error reading JSON file (%d): %s",
			error.line, error.text);
		return NULL;
	}

	return root;
}

static json_t *get_ingest_servers(void)
{
	char *file;
	json_t *root = NULL, *ingests = NULL;

	file = obs_module_config_path("services.json");
	if (file) {
		root = open_json_file(file);
		bfree(file);
	}

	if (!root) {
		file = obs_module_file("services.json");
		if (file) {
			root = open_json_file(file);
			bfree(file);
		}
	}

	ingests = json_object_get(root, "ingests");
	if (ingests)
		json_incref(ingests);
	json_decref(root);

	if (!ingests) {
		blog(LOG_WARNING, "ftl-beam.c: [open_json_file] "
			"No ingests list");
		return NULL;
	}

	return ingests;
}

static obs_properties_t *ftl_beam_properties(void *unused)
{
	json_t *ingests = NULL;
	UNUSED_PARAMETER(unused);

	obs_properties_t *ppts = obs_properties_create();
	obs_property_t *p;

	obs_properties_add_list(ppts, "server", obs_module_text("Server"),
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);

	ingests = get_ingest_servers();
	
	if (ingests) {
		fill_servers(obs_properties_get(ppts, "server"), ingests, NULL);
	}

	obs_properties_add_text(ppts, "key", obs_module_text("StreamKey"),
			OBS_TEXT_PASSWORD);
	return ppts;
}

static const char *ftl_beam_url(void *data)
{
	struct ftl_beam *service = data;
	return service->server;
}

static const char *ftl_beam_key(void *data)
{
	struct ftl_beam *service = data;
	return service->key;
}

static void ftl_beam_apply_settings(void *data,
		obs_data_t *video_settings, obs_data_t *audio_settings)
{
	struct ftl_beam *service = data;
	/*
	json_t             *root = open_services_file();

	if (root) {
		initialize_output(service, root, video_settings,
				audio_settings);
		json_decref(root);
	}
	*/
}
/*
static void add_service(obs_property_t *list, json_t *service, bool show_all,
	const char *cur_service)
{
	json_t *servers;
	const char *name;
	bool common;

	if (!json_is_object(service)) {
		blog(LOG_WARNING, "rtmp-common.c: [add_service] service "
			"is not an object");
		return;
	}

	name = get_string_val(service, "name");
	if (!name) {
		blog(LOG_WARNING, "rtmp-common.c: [add_service] service "
			"has no name");
		return;
	}

	common = get_bool_val(service, "common");
	if (!show_all && !common && strcmp(cur_service, name) != 0) {
		return;
	}

	servers = json_object_get(service, "servers");
	if (!servers || !json_is_array(servers)) {
		blog(LOG_WARNING, "rtmp-common.c: [add_service] service "
			"'%s' has no servers", name);
		return;
	}

	obs_property_list_add_string(list, name, name);
}

static bool service_selected(obs_properties_t *props, obs_property_t *p,
obs_data_t *settings)
{
const char *name = obs_data_get_string(settings, "service");
json_t *root     = obs_properties_get_param(props);
json_t *service;

if (!name || !*name)
return false;

service = find_service(root, name);
if (!service)
return false;

fill_servers(obs_properties_get(props, "server"), service, name);

UNUSED_PARAMETER(p);
return true;
}

static void fill_servers(obs_property_t *servers_prop, json_t *service, const char *name)
{
	json_t *servers, *server;
	size_t index;

	obs_property_list_clear(servers_prop);

	servers = json_object_get(service, "servers");

	if (!json_is_array(servers)) {
		blog(LOG_WARNING, "rtmp-common.c: [fill_servers] "
		"Servers for service '%s' not a valid object",
		name);
		return;
	}

	json_array_foreach (servers, index, server) {
		const char *server_name = get_string_val(server, "name");
		const char *url         = get_string_val(server, "url");

		if (!server_name || !url)
			continue;

		obs_property_list_add_string(servers_prop, server_name, url);
	}
}
*/

static inline const char *get_string_val(json_t *service, const char *key)
{
	json_t *str_val = json_object_get(service, key);
	if (!str_val || !json_is_string(str_val))
		return NULL;

	return json_string_value(str_val);
}

static void fill_servers(obs_property_t *servers_prop, json_t *ingests, const char *name) {
	json_t *ingest;
	size_t index;

	obs_property_list_clear(servers_prop);

	json_array_foreach(ingests, index, ingest) {
		const char *name = get_string_val(ingest, "name");
		const char *host = get_string_val(ingest, "host");

		if (!name || !host)
			continue;

		obs_property_list_add_string(servers_prop, name, host);
	}
}

struct obs_service_info ftl_beam_service = {
	.id             = "ftl_beam",
	.get_name       = ftl_beam_name,
	.create         = ftl_beam_create,
	.destroy        = ftl_beam_destroy,
	.update         = ftl_beam_update,
	.get_properties = ftl_beam_properties,
	.get_url        = ftl_beam_url,
	.get_key        = ftl_beam_key,
	.apply_encoder_settings = ftl_beam_apply_settings
};