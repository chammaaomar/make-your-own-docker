#include "requests.h"

size_t write_to_buffer(void* data, size_t size, size_t nmemb, void* array_mem) {
	// libcurl always calls the callback with size == 1;
	size_t true_sz = size*nmemb;
	BufferArray* buf_arr = (BufferArray*) array_mem;
	memcpy(buf_arr->buffer + buf_arr->size, data, true_sz);
	buf_arr->size += true_sz;
	return true_sz;
}

size_t write_to_file(void* data, size_t size, size_t nmemb, void* file_mem) {
	return fwrite(data, size, nmemb, (FILE*) file_mem);
}

char* extract_string_from_json(char** buffer_ptr, char* field) {
	/*
	Side Effect: Moves the buffer pointed to by @buffer_ptr to the end of
	the target. Thus the pointer referenced by @buffer_ptr acts like a
	cursor that can be iteratively used to search a JSON response for more
	fields until exhausted.
	*/
	// e.g. "token": thus strlen(token) + 3 chars + null terminator
	char* buffer = *buffer_ptr;
	char* start_str = (char*) calloc(strlen(field) + 4, sizeof(char));
	start_str[0] = '"';
	strcat(start_str, field);
	strcat(start_str, "\":");
	char* target_start = strstr(buffer, start_str);
	if (!target_start) {
		fprintf(stderr, "Could not find field: %s in JSON response\n", field);
		return NULL;
	}
	target_start += strlen(start_str);
	target_start = strchr(target_start, '"') + 1;
	char* target_end = strchr(target_start, '"');
	// the access token ends with $TOKEN" so the pointer is pointing to -> " at the end
	// so we have to displace it one step backward.
	target_end += -1;
	size_t target_len = (size_t) (target_end - target_start);
	if (target_len > 0) {
		char* target = (char*) calloc(target_len + 2, sizeof(char));
		memcpy(target, target_start, target_len + 1);
		// displace the buffer to the end of the target, so it can be used
		// to search iteratively for searching next instance of the field
		*buffer_ptr = target_end + 1;
		return target;
	}
	
	fprintf(stderr, "Error: target string (JSON response) had length: %i\n", (int) target_len);
	return NULL;

}

char* make_curl_req(char* url, size_t write_cb (void*, size_t, size_t, void*), struct curl_slist* list, char* filename) {
	CURL* curl = curl_easy_init();
	FILE* out_file;
	if (filename) {
		out_file = fopen(filename, "ab");
	}
	char* body_buffer = (char*) calloc(CURL_MAX_WRITE_SIZE, sizeof(char));
	BufferArray buf_arr;
	buf_arr.buffer = body_buffer;
	buf_arr.size = 0;
	if (curl) {
		CURLcode res;
		if (list) {
			curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);
		}
		curl_easy_setopt(curl, CURLOPT_URL, url);
		curl_easy_setopt(curl, CURLOPT_VERBOSE, 0L);
		curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
		curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
		if (!filename) {
			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
			curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*) &buf_arr);
			
			res = curl_easy_perform(curl);
			if (res != CURLE_OK) {
				fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
			}
			curl_easy_cleanup(curl);
			return body_buffer;
		}
		else {
			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_to_file);
			curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*) out_file);
		
			res = curl_easy_perform(curl);
			if (res != CURLE_OK) {
				fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
			}
			curl_easy_cleanup(curl);
			fclose(out_file);
			return filename;
		}
	}
}