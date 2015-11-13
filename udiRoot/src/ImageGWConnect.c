#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <munge.h>
#include <curl/curl.h>
#include <json-c/json.h>

#include "ImageData.h"
#include "utility.h"

typedef struct _ImageGwState {
    char *message;
    int isJsonMessage;
    size_t expContentLen;
    size_t messageLen;
    size_t messageCurr;
    int messageComplete;
} ImageGwState;

size_t handleResponseHeader(char *ptr, size_t sz, size_t nmemb, void *data) {
    ImageGwState *imageGw = (ImageGwState *) data;
    if (imageGw == NULL) {
        return 0;
    }
    if (strncmp(ptr, "HTTP", 4) == 0) {
    }

    char *colon = strchr(ptr, ':');
    char *key = NULL, *value = NULL;
    if (colon != NULL) {
        *colon = 0;
        value = colon + 1;
        key = shifter_trim(ptr);
        value = shifter_trim(colon + 1);
        if (strcasecmp(key, "Content-Type") == 0 && strcmp(value, "application/json") == 0) {
            imageGw->isJsonMessage = 1;
        }
        if (strcasecmp(key, "Content-Length") == 0) {
            imageGw->expContentLen = strtoul(value, NULL, 10);
        }
    }
    return nmemb;
}

size_t handleResponseData(char *ptr, size_t sz, size_t nmemb, void *data) {
    ImageGwState *imageGw = (ImageGwState *) data;
    if (imageGw == NULL || imageGw->messageComplete) {
        return 0;
    }
    if (sz != sizeof(char)) {
        return 0;
    }

    size_t before = imageGw->messageCurr;
    imageGw->message = alloc_strcatf(imageGw->message, &(imageGw->messageCurr), &(imageGw->messageLen), "%s", ptr);
    if (before + nmemb != imageGw->messageCurr) {
        /* error */
        return 0;
    }

    if (imageGw->messageCurr == imageGw->expContentLen) {
        imageGw->messageComplete = 1;
    }
    return nmemb;
}

ImageData *parseLookupResponse(ImageGwState *imageGw) {
    if (imageGw == NULL || !imageGw->isJsonMessage || !imageGw->messageComplete) {
        return NULL;
    }
    json_object *jObj = json_tokener_parse(imageGw->message);
    json_object_iter jIt;
    ImageData *image = NULL;

    if (jObj == NULL) {
        return NULL;
    }
    /*
    {
        "ENTRY": null,
        "ENV": null,
        "groupAcl": [],
        "id": "a5a467fddcb8848a80942d0191134c925fa16ffa9655c540acd34284f4f6375d",
        "itype": "docker",
        "last_pull": 1446245766.1146851,
        "status": "READY",
        "system": "cori",
        "tag": "ubuntu:14.04",
        "userAcl": []
    }
    */

    image = (ImageData *) malloc(sizeof(ImageData));
    json_object_object_foreachC(jObj, jIt) {
        enum json_type type = json_object_get_type(jIt.val);
        if (strcmp(jIt.key, "status") == 0 && type == json_type_string) {
            const char *val = json_object_get_string(jIt.val);
            if (val != NULL) {
                image->status = strdup(val);
            }
        } else if (strcmp(jIt.key, "id") == 0 && type == json_type_string) {
            const char *val = json_object_get_string(jIt.val);
            if (val != NULL) {
                image->identifier = strdup(val);
            }
        } else if (strcmp(jIt.key, "tag") == 0 && type == json_type_string) {
            const char *val = json_object_get_string(jIt.val);
            if (val != NULL) {
                image->tag = strdup(val);
            }
        } else if (strcmp(jIt.key, "itype") == 0) {
            const char *val = json_object_get_string(jIt.val);
            if (val != NULL) {
                image->type = strdup(val);
            }
        }
    }

    json_object_put(jObj);  /* apparently this weirdness frees the json object */
    return image;
}

int main(int argc, char **argv) {
    char *cred = NULL;
    CURL *curl = NULL;
    CURLcode err;
    struct curl_slist *headers = NULL;
    const char *url = "http://cori18-224.nersc.gov:5000/api/lookup/cori/docker/ubuntu:14.04/";
    char *authstr = NULL;
    size_t authstr_len = 0;

    ImageGwState *imageGw = (ImageGwState *) malloc(sizeof(ImageGwState));
    memset(imageGw, 0, sizeof(ImageGwState));

    curl_global_init(CURL_GLOBAL_ALL);
    curl = curl_easy_init();


    curl_easy_setopt(curl, CURLOPT_URL, url);

    munge_ctx_t ctx = munge_ctx_create();
    munge_encode(&cred, ctx, "", 0); 
    authstr = alloc_strgenf("authentication:%s", cred);
    if (authstr == NULL) {
        exit(1);
    }
    free(cred);
    munge_ctx_destroy(ctx);

    headers = curl_slist_append(headers, authstr);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, handleResponseHeader);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, imageGw);

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, handleResponseData);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, imageGw);

    err = curl_easy_perform(curl);
    if (err) {
        exit(1);
    }
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    if (http_code == 200) {
        if (imageGw->messageComplete) {
            ImageData *image = parseLookupResponse(imageGw);
            if (image != NULL) {
                printf("image: %s:%s === %s\n", image->type, image->tag, image->identifier);
            }
        }
    } else {
        exit(1);
    }


    curl_global_cleanup();
    return 0;
}
