#include <obs-module.h>

struct ftl_beam {
	char *server, *key;
	bool use_auth;
	char *username, *password;
};

static const char *ftl_beam_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("Beam FTL Server");
}

static void ftl_beam_update(void *data, obs_data_t *settings)
{
	struct ftl_beam *service = data;

	bfree(service->server);
	bfree(service->key);

	service->server = bstrdup(obs_data_get_string(settings, "server"));
	service->key    = bstrdup(obs_data_get_string(settings, "key"));
	service->use_auth = obs_data_get_bool(settings, "use_auth");
	service->username = bstrdup(obs_data_get_string(settings, "username"));
	service->password = bstrdup(obs_data_get_string(settings, "password"));
}

static void ftl_beam_destroy(void *data)
{
	struct ftl_beam *service = data;

	bfree(service->server);
	bfree(service->key);
	bfree(service->username);
	bfree(service->password);
	bfree(service);
}

static void *ftl_beam_create(obs_data_t *settings, obs_service_t *service)
{
	struct ftl_beam *data = bzalloc(sizeof(struct ftl_beam));
	ftl_beam_update(data, settings);

	UNUSED_PARAMETER(service);
	return data;
}

static bool use_auth_modified(obs_properties_t *ppts, obs_property_t *p,
	obs_data_t *settings)
{
	bool use_auth = obs_data_get_bool(settings, "use_auth");
	p = obs_properties_get(ppts, "username");
	obs_property_set_visible(p, use_auth);
	p = obs_properties_get(ppts, "password");
	obs_property_set_visible(p, use_auth);
	return true;
}

static obs_properties_t *ftl_beam_properties(void *unused)
{
	UNUSED_PARAMETER(unused);

	obs_properties_t *ppts = obs_properties_create();
	obs_property_t *p;

	obs_properties_add_text(ppts, "server", "URL", OBS_TEXT_DEFAULT);

	obs_properties_add_text(ppts, "key", obs_module_text("StreamKey"),
			OBS_TEXT_PASSWORD);

	p = obs_properties_add_bool(ppts, "use_auth", obs_module_text("UseAuth"));
	obs_properties_add_text(ppts, "username", obs_module_text("Username"),
			OBS_TEXT_DEFAULT);
	obs_properties_add_text(ppts, "password", obs_module_text("Password"),
			OBS_TEXT_PASSWORD);
	obs_property_set_modified_callback(p, use_auth_modified);
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

static const char *ftl_beam_username(void *data)
{
	struct ftl_beam *service = data;
	if (!service->use_auth)
		return NULL;
	return service->username;
}

static const char *ftl_beam_password(void *data)
{
	struct ftl_beam *service = data;
	if (!service->use_auth)
		return NULL;
	return service->password;
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

struct obs_service_info ftl_beam_service = {
	.id             = "ftl_beam",
	.get_name       = ftl_beam_name,
	.create         = ftl_beam_create,
	.destroy        = ftl_beam_destroy,
	.update         = ftl_beam_update,
	.get_properties = ftl_beam_properties,
	.get_url        = ftl_beam_url,
	.get_key        = ftl_beam_key,
	.get_username   = ftl_beam_username,
	.get_password   = ftl_beam_password,
	.apply_encoder_settings = ftl_beam_apply_settings
};