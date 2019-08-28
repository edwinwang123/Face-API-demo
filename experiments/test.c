#include <json/json.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main() {
	json_object * arr = json_object_new_array();
	json_object * obj;
	json_object * tmp;

	obj = json_object_new_object();
	tmp = json_object_new_string("test1");

	json_object_object_add(obj, "id", tmp);

	json_object_array_add(arr, obj);

	obj = json_object_new_object();
	tmp = json_object_new_string("test2");

	json_object_object_add(obj, "id", tmp);

	json_object_array_add(arr, obj);

	// check seg fault location
	printf("check\n");

	// actual testing
	json_object_object_foreach(arr, key, val) {
		printf("%s: %s\n", key, json_object_to_json_string(val));
	}

	return 0;
}
