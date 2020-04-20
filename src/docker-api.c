#include "docker-api.h"

char* compose_path(char* base, char* img, char* sep, char* ext) {
	/*
	Builds the url for a request to the docker API v2. If making a request to the auth server,
	the ext should be an action preceded by ':', like ':pull' or ':push'. If making a request
	to the registry, the ext should be a resource like /manifests/latest.
	
	params:
		@base: base url, including protocol.
		@img: assumed to be Official Docker image, so it falls under the library repo
		@ext: e.g. ":pull" or "/tags/list". Must be preceded by ':' or '/'
	*/
	char* url = (char*) calloc(strlen(base) + strlen(img) + strlen(sep) + strlen(ext) + 1, sizeof(char));
	strcat(url, base);
	strcat(url, img);
	strcat(url, sep);
	strcat(url, ext);

	return url;
}

void pull_docker_image(char* img) {
	char* auth_url = compose_path(
						AUTH_PATH,
						img,
						":",
						"pull");
	char* auth_response = make_curl_req(auth_url, write_to_buffer, NULL, NULL);

	char* token = extract_string_from_json(&auth_response, "token");

	if (!token) {
		// failed to get auth token; extract_token already writes to stderr
		// so just exit
		free(token);
		exit(1);
	}
	char* manifests_url = compose_path(DOCKER_BASE, img, "/", "manifests/latest");
	struct curl_slist* headers = NULL;
	// Authorization: Bearer $TOKEN
	char* auth_header = (char*) calloc(strlen(token) + 50, sizeof(char));
	strcpy(auth_header, "Authorization: Bearer ");
	strcat(auth_header, token);
	headers = curl_slist_append(headers, auth_header);

	char* manifests_response = make_curl_req(manifests_url, write_to_buffer, headers, NULL);
	char* digest;
	char* layer_url;
	char* digest_head = (char*) calloc(7, sizeof(char));
	char* filepath;
	do {
		digest = extract_string_from_json(&manifests_response, "blobSum");
		if (digest) {
			layer_url = compose_path(DOCKER_BASE, img, "/blobs/", digest);
			sprintf(digest_head, "%.6s", digest+strlen("sha256:"));
			filepath = make_curl_req(layer_url, write_to_file, headers, digest_head);
		}
	} while (digest != NULL);

	char command[50] = {0};

	strcat(command, "tar xf ");
	strcat(command, filepath);
	strcat(command, " -C tmp-chroot-cage/");
	system(command);


	curl_slist_free_all(headers);
	free(digest_head);
	free(manifests_url);
	free(auth_url);
	free(auth_header);
	free(token);
	

}