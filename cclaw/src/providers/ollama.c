// ollama.c - Ollama Provider implementation
// SPDX-License-Identifier: MIT

#include "providers/ollama.h"
#include "providers/base.h"
#include "core/error.h"
#include "json_config.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define OLLAMA_BASE_URL "http://localhost:11434/v1"
#define OLLAMA_API_BASE_URL "http://localhost:11434"

typedef struct ollama_data_t {
    bool include_reasoning;
} ollama_data_t;

static str_t ollama_get_name(void);
static str_t ollama_get_version(void);
err_t ollama_create(const provider_config_t* config, provider_t** out_provider);
void ollama_destroy(provider_t* provider);
static err_t ollama_connect(provider_t* provider);
static void ollama_disconnect(provider_t* provider);
static bool ollama_is_connected(provider_t* provider);
static err_t ollama_chat(provider_t* provider,
                         const chat_message_t* messages,
                         uint32_t message_count,
                         const tool_def_t* tools,
                         uint32_t tool_count,
                         const char* model,
                         double temperature,
                         chat_response_t** out_response);
static err_t ollama_chat_stream(provider_t* provider,
                                const chat_message_t* messages,
                                uint32_t message_count,
                                const char* model,
                                double temperature,
                                void (*on_chunk)(const char* chunk, void* user_data),
                                void* user_data);
static err_t ollama_list_models(provider_t* provider, str_t** out_models, uint32_t* out_count);
static bool ollama_supports_model(provider_t* provider, const char* model);
static err_t ollama_health_check(provider_t* provider, bool* out_healthy);
static const char** ollama_get_available_models(uint32_t* out_count);

static const provider_vtable_t ollama_vtable = {
    .get_name = ollama_get_name,
    .get_version = ollama_get_version,
    .create = ollama_create,
    .destroy = ollama_destroy,
    .connect = ollama_connect,
    .disconnect = ollama_disconnect,
    .is_connected = ollama_is_connected,
    .chat = ollama_chat,
    .chat_stream = ollama_chat_stream,
    .list_models = ollama_list_models,
    .supports_model = ollama_supports_model,
    .health_check = ollama_health_check,
    .get_available_models = ollama_get_available_models
};

const provider_vtable_t* ollama_get_vtable(void) {
    return &ollama_vtable;
}

static str_t ollama_get_name(void) {
    return STR_LIT("ollama");
}

static str_t ollama_get_version(void) {
    return STR_LIT("1.0.0");
}

err_t ollama_create(const provider_config_t* config, provider_t** out_provider) {
    if (!config || !out_provider) return ERR_INVALID_ARGUMENT;

    provider_t* provider = calloc(1, sizeof(provider_t));
    if (!provider) return ERR_OUT_OF_MEMORY;

    provider->vtable = &ollama_vtable;
    provider->config = *config;

    if (str_empty(config->base_url)) {
        provider->config.base_url = (str_t){ .data = OLLAMA_BASE_URL, .len = strlen(OLLAMA_BASE_URL) };
    }

    http_client_config_t http_config = http_client_default_config();
    http_config.timeout_ms = config->timeout_ms > 0 ? config->timeout_ms : 120000;
    provider->http = http_client_create(&http_config);
    if (!provider->http) {
        free(provider);
        return ERR_NETWORK;
    }

    if (!str_empty(config->api_key)) {
        char auth_header[512];
        snprintf(auth_header, sizeof(auth_header), "Bearer %.*s", (int)config->api_key.len, config->api_key.data);
        http_client_add_header(provider->http, "Authorization", auth_header);
    }

    http_client_add_header(provider->http, "Content-Type", "application/json");

    ollama_data_t* data = calloc(1, sizeof(ollama_data_t));
    if (data) {
        provider->impl_data = data;
    }

    *out_provider = provider;
    return ERR_OK;
}

void ollama_destroy(provider_t* provider) {
    if (!provider) return;

    if (provider->http) {
        http_client_destroy(provider->http);
    }

    if (provider->impl_data) {
        free(provider->impl_data);
    }

    free(provider);
}

static err_t ollama_connect(provider_t* provider) {
    if (!provider) return ERR_INVALID_ARGUMENT;

    http_response_t* response = NULL;
    char url[512];
    snprintf(url, sizeof(url), "%s/models", OLLAMA_API_BASE_URL);

    err_t err = http_get(provider->http, url, &response);
    if (err == ERR_OK && response) {
        provider->connected = http_response_is_success(response);
        http_response_free(response);
    } else {
        provider->connected = false;
    }

    return provider->connected ? ERR_OK : ERR_NETWORK;
}

static void ollama_disconnect(provider_t* provider) {
    if (provider) {
        provider->connected = false;
    }
}

static bool ollama_is_connected(provider_t* provider) {
    return provider && provider->connected;
}

static char* build_ollama_request(const provider_t* provider,
                                  const chat_message_t* messages,
                                  uint32_t message_count,
                                  const char* model,
                                  double temperature,
                                  bool stream) {
    (void)provider;

    json_value_t* root = json_create_object();
    if (!root) return NULL;

    json_object_set_string(root, "model", model);
    json_object_set_number(root, "temperature", temperature);
    json_object_set_bool(root, "stream", stream);

    json_value_t* msgs = json_create_array();
    for (uint32_t i = 0; i < message_count; i++) {
        json_value_t* msg = json_create_object();
        const char* role_str = "user";
        if (messages[i].role == CHAT_ROLE_SYSTEM) role_str = "system";
        else if (messages[i].role == CHAT_ROLE_ASSISTANT) role_str = "assistant";
        else if (messages[i].role == CHAT_ROLE_TOOL) role_str = "tool";

        json_object_set_string(msg, "role", role_str);
        json_object_set_string(msg, "content", messages[i].content.data);
        json_array_append(msgs, msg);
    }
    json_object_set(root, "messages", msgs);

    char* json_str = json_print(root, false);
    json_free(root);
    return json_str;
}

static err_t ollama_chat(provider_t* provider,
                         const chat_message_t* messages,
                         uint32_t message_count,
                         const tool_def_t* tools,
                         uint32_t tool_count,
                         const char* model,
                         double temperature,
                         chat_response_t** out_response) {
    if (!provider || !provider->http || !out_response) return ERR_INVALID_ARGUMENT;
    (void)tools;
    (void)tool_count;

    const char* model_id = model && strlen(model) > 0 ? model :
        (provider->config.default_model.data ? provider->config.default_model.data : "llama3.2");

    char* request_body = build_ollama_request(provider, messages, message_count, model_id, temperature, false);
    if (!request_body) return ERR_OUT_OF_MEMORY;

    char url[512];
    snprintf(url, sizeof(url), "%s/chat/completions", OLLAMA_API_BASE_URL);

    http_response_t* http_resp = NULL;
    err_t err = http_post_json(provider->http, url, request_body, &http_resp);
    free(request_body);

    if (err != ERR_OK) return err;

    chat_response_t* response = chat_response_create();
    if (!response) {
        http_response_free(http_resp);
        return ERR_OUT_OF_MEMORY;
    }

    if (http_response_is_success(http_resp)) {
        json_value_t* root = json_parse(http_resp->body.data);
        if (root) {
            json_object_t* obj = json_as_object(root);
            if (obj) {
                json_array_t* choices = json_object_get_array(obj, "choices");
                if (choices && json_array_length(choices) > 0) {
                    json_value_t* first = json_array_get(choices, 0);
                    json_object_t* choice_obj = json_as_object(first);
                    if (choice_obj) {
                        json_object_t* message = json_object_get_object(choice_obj, "message");
                        if (message) {
                            const char* content = json_object_get_string(message, "content", "");
                            if (content) {
                                response->content = (str_t){ .data = strdup(content), .len = strlen(content) };
                            }
                        }
                        const char* finish = json_object_get_string(choice_obj, "finish_reason", "stop");
                        response->finish_reason = (str_t){ .data = strdup(finish), .len = strlen(finish) };
                    }
                }
            }
            json_free(root);
        }
        response->model = (str_t){ .data = strdup(model_id), .len = strlen(model_id) };
    }

    http_response_free(http_resp);
    *out_response = response;
    return ERR_OK;
}

typedef struct {
    void (*on_chunk)(const char*, void*);
    void* user_data;
    chat_response_t* response;
} ollama_stream_ctx_t;

static size_t ollama_sse_parser_write(const char* ptr, size_t size, void* userdata) {
    size_t bytes = size;
    ollama_stream_ctx_t* ctx = (ollama_stream_ctx_t*)userdata;

    if (ctx && ctx->on_chunk && ptr) {
        ctx->on_chunk(ptr, ctx->user_data);
    }

    return bytes;
}

static err_t ollama_chat_stream(provider_t* provider,
                                const chat_message_t* messages,
                                uint32_t message_count,
                                const char* model,
                                double temperature,
                                void (*on_chunk)(const char* chunk, void* user_data),
                                void* user_data) {
    if (!provider || !provider->http || !on_chunk) return ERR_INVALID_ARGUMENT;

    const char* model_id = model && strlen(model) > 0 ? model :
        (provider->config.default_model.data ? provider->config.default_model.data : "llama3.2");

    char* request_body = build_ollama_request(provider, messages, message_count, model_id, temperature, true);
    if (!request_body) return ERR_OUT_OF_MEMORY;

    char url[512];
    snprintf(url, sizeof(url), "%s/chat/completions", OLLAMA_API_BASE_URL);

    ollama_stream_ctx_t ctx = {
        .on_chunk = on_chunk,
        .user_data = user_data,
        .response = NULL
    };

    err_t err = http_post_json_stream(provider->http, url, request_body, ollama_sse_parser_write, &ctx);
    free(request_body);

    return err;
}

static err_t ollama_list_models(provider_t* provider, str_t** out_models, uint32_t* out_count) {
    if (!provider || !out_models || !out_count) return ERR_INVALID_ARGUMENT;

    http_response_t* response = NULL;
    char url[512];
    snprintf(url, sizeof(url), "%s/api/tags", OLLAMA_API_BASE_URL);

    err_t err = http_get(provider->http, url, &response);
    if (err != ERR_OK || !response) {
        *out_models = NULL;
        *out_count = 0;
        return ERR_NETWORK;
    }

    str_t* models = NULL;
    uint32_t count = 0;

    json_value_t* json = json_parse(response->body.data);
    if (json) {
        json_object_t* root = json_as_object(json);
        if (root) {
            json_value_t* models_val = json_object_get(root, "models");
            if (models_val && json_is_array(models_val)) {
                json_array_t* models_arr = json_as_array(models_val);
                count = (uint32_t)json_array_length(models_arr);
                if (count > 0) {
                    models = calloc(count, sizeof(str_t));
                    if (models) {
                        for (uint32_t i = 0; i < count; i++) {
                            json_value_t* model_obj = json_array_get(models_arr, i);
                            json_object_t* model_item = json_as_object(model_obj);
                            if (model_item) {
                                const char* name = json_object_get_string(model_item, "name", NULL);
                                if (name) {
                                    models[i] = (str_t){ .data = strdup(name), .len = strlen(name) };
                                }
                            }
                        }
                    } else {
                        count = 0;
                    }
                }
            }
        }
        json_free(json);
    }

    http_response_free(response);
    *out_models = models;
    *out_count = count;
    return ERR_OK;
}

static bool ollama_supports_model(provider_t* provider, const char* model) {
    (void)provider;
    (void)model;
    return true;
}

static err_t ollama_health_check(provider_t* provider, bool* out_healthy) {
    if (!provider || !out_healthy) return ERR_INVALID_ARGUMENT;

    http_response_t* response = NULL;
    char url[512];
    snprintf(url, sizeof(url), "%s/api/tags", OLLAMA_API_BASE_URL);

    err_t err = http_get(provider->http, url, &response);
    if (err == ERR_OK && response) {
        *out_healthy = http_response_is_success(response);
        http_response_free(response);
    } else {
        *out_healthy = false;
    }

    return ERR_OK;
}

static const char* default_models[] = {
    "llama3.2",
    "llama3.1",
    "qwen2.5",
    "mistral",
    "codellama",
    "phi3",
    "gemma2"
};

static const char** ollama_get_available_models(uint32_t* out_count) {
    if (out_count) *out_count = sizeof(default_models) / sizeof(default_models[0]);
    return default_models;
}
