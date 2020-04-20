#include "base.h"
#include <curl/curl.h>

typedef struct BufferArray {
	char* buffer;
	int size;
} BufferArray;

size_t write_to_buffer(void* data, size_t size, size_t nmemb, void* array_mem);

size_t write_to_file(void* data, size_t size, size_t nmemb, void* file_mem);

char* extract_string_from_json(char** buffer_ptr, char* field);

char* make_curl_req(char* url,
					size_t write_cb (void*, size_t, size_t, void*),
					struct curl_slist* list,
					char* filename);

