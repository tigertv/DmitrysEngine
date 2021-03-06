/* Copyright (c) 2017-2019 Dmitry Stepanov a.k.a mr.DIMAS
*
* Permission is hereby granted, free of charge, to any person obtaining
* a copy of this software and associated documentation files (the
* "Software"), to deal in the Software without restriction, including
* without limitation the rights to use, copy, modify, merge, publish,
* distribute, sublicense, and/or sell copies of the Software, and to
* permit persons to whom the Software is furnished to do so, subject to
* the following conditions:
*
* The above copyright notice and this permission notice shall be
* included in all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
* NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
* LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
* OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
* WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE. */

static de_object_visitor_node_t* de_object_visitor_node_create(const char* name)
{
	de_object_visitor_node_t* node = DE_NEW(de_object_visitor_node_t);
	de_str8_set(&node->name, name);
	return node;
}

static void de_object_visitor_node_free(de_object_visitor_node_t* node)
{
	/* free children */
	for (size_t i = 0; i < node->children.size; ++i) {
		de_object_visitor_node_free(node->children.data[i]);
	}
	DE_ARRAY_FREE(node->children);

	/* free records */
	for (size_t i = 0; i < node->fields.size; ++i) {
		de_str8_free(&node->fields.data[i].name);
	}
	DE_ARRAY_FREE(node->fields);

	de_str8_free(&node->name);
	de_free(node);
}

static void de_object_visitor_node_add_child(de_object_visitor_node_t* node, de_object_visitor_node_t* child)
{
	DE_ARRAY_APPEND(node->children, child);
	child->parent = node;
}

static de_object_visitor_field_t* de_object_visitor_node_find_record(de_object_visitor_node_t* node, const char* name)
{
	size_t i;

	for (i = 0; i < node->fields.size; ++i) {
		de_object_visitor_field_t* field = node->fields.data + i;

		if (de_str8_eq(&field->name, name)) {
			return field;
		}
	}

	return NULL;
}

static bool de_object_visitor_visit_data(de_object_visitor_t* visitor,
	const char* name,
	void* data,
	size_t data_size,
	de_object_visitor_data_type_t type)
{
	de_object_visitor_node_t* node = visitor->current_node;

	if (visitor->is_reading) {
		de_object_visitor_field_t* field = de_object_visitor_node_find_record(node, name);

		if (field) {
			if (field->data_type == type) {
				if (field->data_size == data_size) {
					memcpy(data, visitor->data + field->data_offset, data_size);
				} else {
					de_log("serializer error: mismatch data size, but types are equal??? fallback to null data.");
					memset(data, 0, data_size);
					return false;
				}
			} else {
				memset(data, 0, data_size);
				de_log("serializer error: mismatch data type! fallback to null data.");
				return false;
			}
		} else {			
			de_log("serializer error: record %s is not found! fallback to null data", name);
			memset(data, 0, data_size);
			return false;
		}
	} else {
		de_object_visitor_field_t field;

		/* dump data to common buffer */
		uint32_t offset = visitor->data_size;
		visitor->data_size += data_size;
		visitor->data = (char*)de_realloc(visitor->data, visitor->data_size);
		memcpy(visitor->data + offset, data, data_size);

		/* and create field */
		field.data_offset = offset;
		field.data_size = data_size;
		field.data_type = type;
		de_str8_init(&field.name);
		de_str8_set(&field.name, name);

		DE_ARRAY_APPEND(node->fields, field);
	}

	return true;
}

bool de_object_visitor_enter_node(de_object_visitor_t* visitor, const char* node_name)
{
	if (visitor->is_reading) {
		bool found = false;

		for (size_t i = 0; i < visitor->current_node->children.size; ++i) {
			de_object_visitor_node_t* child = visitor->current_node->children.data[i];

			if (de_str8_eq(&child->name, node_name)) {
				visitor->current_node = child;

				found = true;

				break;
			}
		}

		if (!found) {
			de_log("serialization: no such child node %s in node %s", node_name, visitor->current_node->name);

			return false;
		}
	} else {
		de_object_visitor_node_t* child = de_object_visitor_node_create(node_name);
		de_object_visitor_node_add_child(visitor->current_node, child);
		visitor->current_node = child;
	}

	return true;
}

void de_object_visitor_leave_node(de_object_visitor_t* visitor)
{
	if (visitor->current_node && visitor->current_node->parent) {
		visitor->current_node = visitor->current_node->parent;
	} else {
		de_log("serialization: mismatch calls of enter/leave node!");
	}
}

bool de_object_visitor_visit_pointer(de_object_visitor_t* visitor, const char* name, void** pointer_ptr, size_t pointee_size, de_visit_callback_t pointee_visitor)
{
	if (!pointee_visitor) {
		de_log("serialization: trying to visit pointer, but visitor function is not specified!");
		return false;
	}

	if (!de_object_visitor_enter_node(visitor, name)) {
		return false;
	}

	/* Visit IntPtr */
	uint64_t int_ptr = 0;
	if (!visitor->is_reading) {
		int_ptr = (uint64_t)((intptr_t)*pointer_ptr);
	}
	if (!de_object_visitor_visit_uint64(visitor, "IntPtr", &int_ptr)) {
		return false;
	}

	/* ignore null pointers */
	if (int_ptr == 0) {
		*pointer_ptr = NULL;
		de_object_visitor_leave_node(visitor);
		return true;
	}

	/* try to find existing pointer. TODO: optimize using binary search? */
	de_pointer_pair_t * existing = NULL;
	de_pointer_pair_t* pointer_pair;
	for (size_t i = 0; i < visitor->pointerPairs.size; ++i) {
		pointer_pair = visitor->pointerPairs.data + i;

		if (pointer_pair->last == int_ptr) {
			existing = pointer_pair;
			break;
		}
	}

	/* try to remap old pointer to new */
	if (visitor->is_reading) {
		if (existing) {
			/* object already created, return pointer to current object's location */
			*pointer_ptr = existing->current;
		} else {
			/* object doesn't exist, create new one */
			*pointer_ptr = de_calloc(1, pointee_size);
		}
	}

	if (!existing) {
		/* add pointer pair to list */
		DE_ARRAY_GROW(visitor->pointerPairs, 1);
		pointer_pair = &DE_ARRAY_LAST(visitor->pointerPairs);
		pointer_pair->current = *pointer_ptr;
		pointer_pair->last = int_ptr;

		/* visit pointee */
		pointee_visitor(visitor, *pointer_ptr);
	}

	de_object_visitor_leave_node(visitor);

	return true;
}

bool de_object_visitor_visit_enum_(de_object_visitor_t* visitor, const char* name, void* enumeration, size_t enumeration_size)
{
	uint64_t data = 0;
	if (!visitor->is_reading) {
		memcpy(&data, enumeration, enumeration_size <= sizeof(data) ? enumeration_size : sizeof(data));
	}
	bool result = de_object_visitor_visit_uint64(visitor, name, &data);
	if(visitor->is_reading) {
		memcpy(enumeration, &data, enumeration_size);
	}
	return result;
}

bool de_object_visitor_visit_int64(de_object_visitor_t* visitor, const char* name, int64_t* integer)
{
	return de_object_visitor_visit_data(visitor, name, integer, sizeof(*integer), DE_OBJECT_VISITOR_DATA_TYPE_INT64);
}

bool de_object_visitor_visit_int32(de_object_visitor_t* visitor, const char* name, int32_t* integer)
{
	return de_object_visitor_visit_data(visitor, name, integer, sizeof(*integer), DE_OBJECT_VISITOR_DATA_TYPE_INT32);
}

bool de_object_visitor_visit_int16(de_object_visitor_t* visitor, const char* name, int16_t* integer)
{
	return de_object_visitor_visit_data(visitor, name, integer, sizeof(*integer), DE_OBJECT_VISITOR_DATA_TYPE_INT16);
}

bool de_object_visitor_visit_int8(de_object_visitor_t* visitor, const char* name, int8_t* integer)
{
	return de_object_visitor_visit_data(visitor, name, integer, sizeof(*integer), DE_OBJECT_VISITOR_DATA_TYPE_INT8);
}

bool de_object_visitor_visit_bool(de_object_visitor_t* visitor, const char* name, bool* boolean)
{
	return de_object_visitor_visit_data(visitor, name, boolean, sizeof(*boolean), DE_OBJECT_VISITOR_DATA_TYPE_BOOL);
}

bool de_object_visitor_visit_uint64(de_object_visitor_t* visitor, const char* name, uint64_t* integer)
{
	return de_object_visitor_visit_data(visitor, name, integer, sizeof(*integer), DE_OBJECT_VISITOR_DATA_TYPE_UINT64);
}

bool de_object_visitor_visit_uint32(de_object_visitor_t* visitor, const char* name, uint32_t* integer)
{
	return de_object_visitor_visit_data(visitor, name, integer, sizeof(*integer), DE_OBJECT_VISITOR_DATA_TYPE_UINT32);
}

bool de_object_visitor_visit_uint16(de_object_visitor_t* visitor, const char* name, uint16_t* integer)
{
	return de_object_visitor_visit_data(visitor, name, integer, sizeof(*integer), DE_OBJECT_VISITOR_DATA_TYPE_UINT16);
}

bool de_object_visitor_visit_uint8(de_object_visitor_t* visitor, const char* name, uint8_t* integer)
{
	return de_object_visitor_visit_data(visitor, name, integer, sizeof(*integer), DE_OBJECT_VISITOR_DATA_TYPE_UINT8);
}

bool de_object_visitor_visit_float(de_object_visitor_t* visitor, const char* name, float* flt)
{
	return de_object_visitor_visit_data(visitor, name, flt, sizeof(*flt), DE_OBJECT_VISITOR_DATA_TYPE_FLOAT);
}

bool de_object_visitor_visit_double(de_object_visitor_t* visitor, const char* name, double* dbl)
{
	return de_object_visitor_visit_data(visitor, name, dbl, sizeof(*dbl), DE_OBJECT_VISITOR_DATA_TYPE_DOUBLE);
}

bool de_object_visitor_visit_vec2(de_object_visitor_t* visitor, const char* name, de_vec2_t* vec)
{
	return de_object_visitor_visit_data(visitor, name, vec, sizeof(*vec), DE_OBJECT_VISITOR_DATA_TYPE_VECTOR2);
}

bool de_object_visitor_visit_vec3(de_object_visitor_t* visitor, const char* name, de_vec3_t* vec)
{
	return de_object_visitor_visit_data(visitor, name, vec, sizeof(*vec), DE_OBJECT_VISITOR_DATA_TYPE_VECTOR3);
}

bool de_object_visitor_visit_vec4(de_object_visitor_t* visitor, const char* name, de_vec4_t* vec)
{
	return de_object_visitor_visit_data(visitor, name, vec, sizeof(*vec), DE_OBJECT_VISITOR_DATA_TYPE_VECTOR4);
}

bool de_object_visitor_visit_quat(de_object_visitor_t* visitor, const char* name, de_quat_t* quat)
{
	return de_object_visitor_visit_data(visitor, name, quat, sizeof(*quat), DE_OBJECT_VISITOR_DATA_TYPE_QUATERNION);
}

bool de_object_visitor_visit_mat3(de_object_visitor_t* visitor, const char* name, de_mat3_t* mat3)
{
	return de_object_visitor_visit_data(visitor, name, mat3, sizeof(*mat3), DE_OBJECT_VISITOR_DATA_TYPE_MATRIX3);
}

bool de_object_visitor_visit_mat4(de_object_visitor_t* visitor, const char* name, de_mat4_t* mat4)
{
	return de_object_visitor_visit_data(visitor, name, mat4, sizeof(*mat4), DE_OBJECT_VISITOR_DATA_TYPE_MATRIX4);
}

bool de_object_visitor_visit_rectf(de_object_visitor_t* visitor, const char* name, de_rectf_t* rect)
{
	return de_object_visitor_visit_data(visitor, name, rect, sizeof(*rect), DE_OBJECT_VISITOR_DATA_TYPE_RECTF);
}

bool de_object_visitor_visit_color(de_object_visitor_t* visitor, const char* name, de_color_t* clr)
{
	return de_object_visitor_visit_data(visitor, name, clr, sizeof(*clr), DE_OBJECT_VISITOR_DATA_TYPE_COLOR);
}

bool de_object_visitor_visit_path(de_object_visitor_t* visitor, const char* name, de_path_t* path)
{
	return de_object_visitor_visit_string(visitor, name, &path->str);
}

bool de_object_visitor_visit_string(de_object_visitor_t* visitor, const char* name, de_str8_t* str)
{
	uint32_t length;

	if (!de_object_visitor_enter_node(visitor, name)) {
		return false;
	}

	/* Visit length */
	if (!visitor->is_reading) {
		length = str->str.size;
	}
	if (!de_object_visitor_visit_uint32(visitor, "Length", &length)) {
		return false;
	}
	if (visitor->is_reading) {
		str->str.size = length ? length - 1 : 0;
		str->str._capacity = length;
	}

	/* Visit data */
	if (visitor->is_reading) {
		str->str.data = (char*)de_malloc(length + 1);
	}
	if (!de_object_visitor_visit_data(visitor, "Data", str->str.data, length, DE_OBJECT_VISITOR_DATA_TYPE_DATA)) {
		return false;
	}

	/* Add null-terminator when reading */
	if (visitor->is_reading) {
		str->str.data[length] = '\0';
	}

	de_object_visitor_leave_node(visitor);

	return true;
}

void de_object_visitor_init(de_core_t* core, de_object_visitor_t* visitor)
{
	memset(visitor, 0, sizeof(*visitor));
	visitor->root = de_object_visitor_node_create("root");
	visitor->current_node = visitor->root;
	visitor->version = DE_OBJECT_VISITOR_VERSION;
	visitor->core = core;
}

void de_object_visitor_free(de_object_visitor_t* visitor)
{
	de_object_visitor_node_free(visitor->root);
	de_free(visitor->data);
	DE_ARRAY_FREE(visitor->pointerPairs);
}

static void de_object_visitor_field_save_binary(de_object_visitor_field_t* field, FILE* file)
{
	uint32_t name_length;
	uint8_t data_type;

	/* write field name */
	name_length = de_str8_length(&field->name);
	fwrite(&name_length, sizeof(name_length), 1, file);
	fwrite(de_str8_cstr(&field->name), name_length, 1, file);

	/* write field data descriptors */
	data_type = (uint8_t)field->data_type;
	fwrite(&field->data_size, sizeof(field->data_size), 1, file);
	fwrite(&data_type, sizeof(data_type), 1, file);
	fwrite(&field->data_offset, sizeof(field->data_offset), 1, file);
}

static void de_object_visitor_node_save_binary(de_object_visitor_node_t* node, FILE* file)
{
	size_t i;
	uint32_t name_length, fields_count, child_count;

	/* write name without null-terminator */
	name_length = de_str8_length(&node->name);
	fwrite(&name_length, sizeof(name_length), 1, file);
	fwrite(de_str8_cstr(&node->name), name_length, 1, file);

	/* write fields */
	fields_count = (uint32_t)node->fields.size;
	fwrite(&fields_count, sizeof(fields_count), 1, file);
	for (i = 0; i < node->fields.size; ++i) {
		de_object_visitor_field_save_binary(node->fields.data + i, file);
	}

	/* write children */
	child_count = (uint32_t)node->children.size;
	fwrite(&child_count, sizeof(child_count), 1, file);
	for (i = 0; i < node->children.size; ++i) {
		de_object_visitor_node_save_binary(node->children.data[i], file);
	}
}

void de_object_visitor_save_binary(de_object_visitor_t* visitor, const char* file_path)
{
	FILE* file = fopen(file_path, "wb");

	fwrite(DE_OBJECT_VISITOR_MAGIC, 7, 1, file);
	fwrite(&visitor->version, sizeof(visitor->data_size), 1, file);
	fwrite(&visitor->data_size, sizeof(visitor->data_size), 1, file);
	de_object_visitor_node_save_binary(visitor->root, file);
	fwrite(visitor->data, visitor->data_size, 1, file);

	fclose(file);
}

static bool de_object_visitor_field_load_binary(de_object_visitor_field_t* field, FILE* file)
{
	/* read name */
	uint32_t name_length;
	if (fread(&name_length, sizeof(name_length), 1, file) != 1) {
		return false;
	}
	de_str8_init(&field->name);
	de_str8_fread(&field->name, file, name_length);

	/* read field data descriptors */
	if (fread(&field->data_size, sizeof(field->data_size), 1, file) != 1) {
		return false;
	}
	
	uint8_t data_type;
	if (fread(&data_type, sizeof(data_type), 1, file) != 1) {
		return false;
	}
	field->data_type = (de_object_visitor_data_type_t)data_type;
	
	if (fread(&field->data_offset, sizeof(field->data_offset), 1, file) != 1) {
		return false;
	}
	
	return true;
}

static bool de_object_visitor_node_load_binary(de_object_visitor_node_t* node, FILE* file)
{
	/* read name */
	uint32_t name_length;
	if (fread(&name_length, sizeof(name_length), 1, file) != 1)  {
		return false;
	}
	de_str8_init(&node->name);
	de_str8_fread(&node->name, file, name_length);

	/* read fields */
	uint32_t fields_count;
	if (fread(&fields_count, sizeof(fields_count), 1, file) != 1) {
		return false;
	}
	DE_ARRAY_GROW(node->fields, fields_count);
	for (size_t i = 0; i < fields_count; ++i) {
		de_object_visitor_field_t* field = node->fields.data + i;
		if (!de_object_visitor_field_load_binary(field, file)) {
			return false;
		}
	}

	/* load children */
	uint32_t child_count;
	if (fread(&child_count, sizeof(child_count), 1, file) != 1) {
		return false;
	}
	for (size_t i = 0; i < child_count; ++i) {
		de_object_visitor_node_t* child = DE_NEW(de_object_visitor_node_t);
		if (!de_object_visitor_node_load_binary(child, file)) {
			de_free(child);
			return false;
		}
		de_object_visitor_node_add_child(node, child);
	}
	
	return true;
}

bool de_object_visitor_load_binary(de_core_t* core, de_object_visitor_t* visitor, const char* file_path)
{	
	memset(visitor, 0, sizeof(*visitor));
	visitor->core = core;
	visitor->is_reading = true;
	
	FILE* file = fopen(file_path, "rb");

	if (!file) {
		de_log("serializer: unable to load binary from %s, file does not exists.", file_path);
		goto error;
	}
	
	char magic[7];
	if (fread(magic, 7, 1, file) != 1) {
		de_log("serializer: unexpected end of file when reading magic");
		goto error;
	}
	if (strncmp(magic, DE_OBJECT_VISITOR_MAGIC, 7) != 0) {
		de_log("serializer: %s is not valid serializer format!", file_path);
		goto error;
	}
	if (fread(&visitor->version, sizeof(visitor->version), 1, file) != 1) {
		de_log("serializer: unexpected end of file when reading version");
		goto error;
	}
	if (fread(&visitor->data_size, sizeof(visitor->data_size), 1, file) != 1) {
		de_log("serializer: unexpected end of file when reading data size");
		goto error;
	}
	visitor->root = DE_NEW(de_object_visitor_node_t);
	if (!de_object_visitor_node_load_binary(visitor->root, file)) {
		goto error;
	}
	visitor->data = (char*)de_malloc(visitor->data_size);
	if (fread(visitor->data, visitor->data_size, 1, file) != 1) {
		de_log("serializer: unexpected end of file when reading data block");        
		goto error;        
	}
	visitor->current_node = visitor->root;

	fclose(file);
	
	return true;
	
error:
	if (file) {
		fclose(file);
	}
	if(visitor->root) {
		de_object_visitor_node_free(visitor->root);
	}
	de_free(visitor->root);
	de_free(visitor->data);
	memset(visitor, 0, sizeof(*visitor));
	return false;
}

bool de_object_visitor_visit_primitive_array(de_object_visitor_t* visitor, const char* name, void** array, size_t* item_count, size_t item_size)
{
	if (!de_object_visitor_enter_node(visitor, name)) {
		return false;
	}

	uint32_t length;
	if (!visitor->is_reading) {
		length = (uint32_t)(*item_count);
	}
	de_object_visitor_visit_uint32(visitor, "Length", &length);
	if (visitor->is_reading) {
		*item_count = length;
	}

	if (visitor->is_reading) {
		*array = de_malloc(item_size * length);
	}

	de_object_visitor_visit_data(visitor, "Bytes", *array, (*item_count) * item_size, DE_OBJECT_VISITOR_DATA_TYPE_DATA);

	de_object_visitor_leave_node(visitor);

	return true;
}

bool de_object_visitor_visit_array(de_object_visitor_t* visitor, const char* name, void** array, size_t* item_count, size_t item_size, de_visit_callback_t callback)
{
	uint32_t i;
	uint32_t length;

	if (!de_object_visitor_enter_node(visitor, name)) {
		return false;
	}

	if (!visitor->is_reading) {
		length = (uint32_t)(*item_count);
	}
	de_object_visitor_visit_uint32(visitor, "Length", &length);
	if (visitor->is_reading) {
		*item_count = length;
	}

	if (visitor->is_reading) {
		*array = de_malloc(item_size * length);
	}
	for (i = 0; i < *item_count; ++i) {
		char item_name[128];
		snprintf(item_name, sizeof(item_name), "Item%d", i);
		de_object_visitor_enter_node(visitor, item_name);

		callback(visitor, (void*)((char*)(*array) + i * item_size));

		de_object_visitor_leave_node(visitor);
	}

	de_object_visitor_leave_node(visitor);

	return true;
}

bool de_object_visitor_visit_pointer_array(de_object_visitor_t* visitor, const char* name, void** array, size_t* item_count, size_t pointee_size, de_visit_callback_t callback)
{	
	if (!de_object_visitor_enter_node(visitor, name)) {
		return false;
	}

	uint32_t length;
	if (!visitor->is_reading) {
		length = (uint32_t)(*item_count);
	}
	de_object_visitor_visit_uint32(visitor, "Length", &length);

	if (visitor->is_reading) {
		if (length == 0) {
			de_object_visitor_leave_node(visitor);
			return true;
		}

		*item_count = length;
		*array = de_calloc(length, sizeof(void*));
	}

	void** pointers = (void**)*array;
	for (uint32_t i = 0; i < *item_count; ++i) {
		char item_name[128];
		void** ptr_ptr = &pointers[i];

		snprintf(item_name, sizeof(item_name), "Item%d", i);

		de_object_visitor_visit_pointer(visitor, item_name, ptr_ptr, pointee_size, callback);
	}

	de_object_visitor_leave_node(visitor);

	return true;
}

bool de_object_visitor_visit_array_ex(de_object_visitor_t* visitor, const char* name, void** array, size_t* item_count, size_t item_size, size_t* capacity, de_visit_callback_t callback)
{
	bool result = true;

	result &= de_object_visitor_visit_array(visitor, name, array, item_count, item_size, callback);

	*capacity = *item_count;

	return result;
}

bool de_object_visitor_visit_primitive_array_ex(de_object_visitor_t* visitor, const char* name, void** array, size_t* item_count, size_t item_size, size_t* capacity)
{
	bool result = true;

	result &= de_object_visitor_visit_primitive_array(visitor, name, array, item_count, item_size);

	*capacity = *item_count;

	return result;
}

bool de_object_visitor_visit_pointer_array_ex(de_object_visitor_t* visitor, const char* name, void** array, size_t* item_count, size_t pointee_size, size_t* capacity, de_visit_callback_t callback)
{
	bool result = true;

	result &= de_object_visitor_visit_pointer_array(visitor, name, array, item_count, pointee_size, callback);

	*capacity = *item_count;

	return result;
}


bool de_object_visitor_visit_intrusive_linked_list(de_object_visitor_t* visitor, const char* name, void** head, void** tail, size_t next_offset, size_t prev_offset, size_t item_size, de_visit_callback_t callback)
{
	uint32_t i;
	bool result = true;
	void* item = NULL, *prev_item = NULL;
	uint32_t length = 0;

	if (!de_object_visitor_enter_node(visitor, name)) {
		return false;
	}

	if (!visitor->is_reading) {
		for (item = *head; item; item = *((void**)((char*)item + next_offset))) {
			++length;
		}
	}

	de_object_visitor_visit_uint32(visitor, "Length", &length);

	if (visitor->is_reading) {
		/* allocate items and link them (restore list on loading) */
		for (i = 0; i < length; ++i) {
			char item_name[128];
			snprintf(item_name, sizeof(item_name), "Item%d", i);
			de_object_visitor_visit_pointer(visitor, item_name, &item, item_size, callback);

			if (i == 0) {
				*head = item;
			}
			if (i == length - 1) {
				*tail = item;
			}

			if (prev_item) {
				/* write next of previous */
				memcpy((char*)prev_item + next_offset, &item, sizeof(void*));
			}

			/* write previous of current */
			memcpy((char*)item + prev_offset, &prev_item, sizeof(void*));

			prev_item = item;
		}
	} else {
		/* visit items */
		for (item = *head, i = 0; item; item = *((void**)((char*)item + next_offset)), ++i) {
			char item_name[128];
			snprintf(item_name, sizeof(item_name), "Item%d", i);
			de_object_visitor_visit_pointer(visitor, item_name, &item, item_size, callback);
		}
	}

	de_object_visitor_leave_node(visitor);

	return result;
}

static void de_object_visitor_node_print(de_object_visitor_t* visitor, de_object_visitor_node_t* node, FILE* stream, int level)
{
	int i;

	for (i = 0; i < level * 2; ++i) {
		fprintf(stream, " ");
	}
	fprintf(stream, "%s: ", de_str8_cstr(&node->name));

	for (i = 0; i < (int)node->fields.size; ++i) {
		de_object_visitor_field_t* field = node->fields.data + i;
		char* field_data = visitor->data + field->data_offset;

		if (i > 0) {
			fprintf(stream, ", ");
		}

		switch (field->data_type) {
			case DE_OBJECT_VISITOR_DATA_TYPE_BOOL:
				fprintf(stream, "<%s|b:%s>", de_str8_cstr(&field->name), *((bool*)field_data) ? "True" : "False");
				break;
			case DE_OBJECT_VISITOR_DATA_TYPE_INT8:
				fprintf(stream, "<%s|i8:%" PRIi8 ">", de_str8_cstr(&field->name), *((int8_t*)field_data));
				break;
			case DE_OBJECT_VISITOR_DATA_TYPE_UINT8:
				fprintf(stream, "<%s|u8:%" PRIu8 ">", de_str8_cstr(&field->name), *((uint8_t*)field_data));
				break;
			case DE_OBJECT_VISITOR_DATA_TYPE_INT16:
				fprintf(stream, "<%s|i16:%" PRIi16 ">", de_str8_cstr(&field->name), *((int16_t*)field_data));
				break;
			case DE_OBJECT_VISITOR_DATA_TYPE_UINT16:
				fprintf(stream, "<%s|u16:%" PRIu16 ">", de_str8_cstr(&field->name), *((uint16_t*)field_data));
				break;
			case DE_OBJECT_VISITOR_DATA_TYPE_INT32:
				fprintf(stream, "<%s|int32:%" PRIi32 ">", de_str8_cstr(&field->name), *((int32_t*)field_data));
				break;
			case DE_OBJECT_VISITOR_DATA_TYPE_UINT32:
				fprintf(stream, "<%s|u32:%" PRIu32 ">", de_str8_cstr(&field->name), *((uint32_t*)field_data));
				break;
			/* temporarily suppress annoying GCC warning about unsupported format. PRIi64/PRIu64 are absolutely safe,
			 * but GCC whines about them */
			#ifdef __GNUC__
			#pragma GCC diagnostic ignored "-Wformat"
			#endif
			case DE_OBJECT_VISITOR_DATA_TYPE_INT64:
				fprintf(stream, "<%s|i64:%" PRIi64 ">", de_str8_cstr(&field->name), *((int64_t*)field_data));
				break;
			case DE_OBJECT_VISITOR_DATA_TYPE_UINT64:
				fprintf(stream, "<%s|u64:%" PRIu64 ">", de_str8_cstr(&field->name), *((uint64_t*)field_data));
				break;
			#ifdef __GNUC__
			#pragma GCC diagnostic warning "-Wformat"
			#endif              
			case DE_OBJECT_VISITOR_DATA_TYPE_FLOAT:
				fprintf(stream, "<%s|f:%f>", de_str8_cstr(&field->name), *((float*)field_data));
				break;
			case DE_OBJECT_VISITOR_DATA_TYPE_DOUBLE:
				fprintf(stream, "<%s|d:%f>", de_str8_cstr(&field->name), *((double*)field_data));
				break;
			case DE_OBJECT_VISITOR_DATA_TYPE_VECTOR2:
			{
				de_vec2_t* v = (de_vec2_t*)field_data;
				fprintf(stream, "<%s|v2:%f,%f>", de_str8_cstr(&field->name), v->x, v->y);
				break;
			}
			case DE_OBJECT_VISITOR_DATA_TYPE_VECTOR3:
			{
				de_vec3_t* v = (de_vec3_t*)field_data;
				fprintf(stream, "<%s|v3:%f,%f,%f>", de_str8_cstr(&field->name), v->x, v->y, v->z);
				break;
			}
			case DE_OBJECT_VISITOR_DATA_TYPE_VECTOR4:
			{
				de_vec4_t* v = (de_vec4_t*)field_data;
				fprintf(stream, "<%s|v4:%f,%f,%f,%f>", de_str8_cstr(&field->name), v->x, v->y, v->z, v->w);
				break;
			}
			case DE_OBJECT_VISITOR_DATA_TYPE_MATRIX3:
			{
				float *v = (float*)field_data;
				fprintf(stream, "<%s|m3:%f,%f,%f,%f,%f,%f,%f,%f,%f>", de_str8_cstr(&field->name), v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8]);
				break;
			}
			case DE_OBJECT_VISITOR_DATA_TYPE_MATRIX4:
			{
				float *v = (float*)field_data;
				fprintf(stream, "<%s|m4:%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f>", de_str8_cstr(&field->name),
					v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8], v[9], v[10], v[11], v[12], v[13], v[14], v[15]);
				break;
			}
			case DE_OBJECT_VISITOR_DATA_TYPE_QUATERNION:
			{
				de_quat_t* q = (de_quat_t*)field_data;
				fprintf(stream, "<%s|q:%f,%f,%f,%f>", de_str8_cstr(&field->name), q->x, q->y, q->z, q->w);
				break;
			}
			case DE_OBJECT_VISITOR_DATA_TYPE_DATA:
			{
				size_t encoded_size;
				char* base64 = de_base64_encode(field_data, field->data_size, &encoded_size);
				fprintf(stream, "<%s|data:%s>", de_str8_cstr(&field->name), base64);
				de_free(base64);
				break;
			}
			default:
				break;
		}

	}

	fprintf(stream, "\n");

	for (i = 0; i < (int)node->children.size; ++i) {
		de_object_visitor_node_print(visitor, node->children.data[i], stream, level + 1);
	}
}

void de_object_visitor_print_tree(de_object_visitor_t* visitor, FILE* stream)
{
	de_object_visitor_node_print(visitor, visitor->root, stream, 0);
}