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

/**
* @brief
*/
typedef struct de_gui_scroll_content_presenter_t {
	de_vec2_t scroll;
	bool can_horizontally_scroll;
	bool can_vertically_scroll;
} de_gui_scroll_content_presenter_t;

struct de_gui_node_dispatch_table_t* de_gui_scroll_content_presenter_get_dispatch_table(void);

/**
* @brief
* @param node
* @param val
*/
void de_gui_scroll_content_presenter_set_v_scroll(de_gui_node_t* node, float val);

/**
* @brief
* @param node
* @param val
*/
void de_gui_scroll_content_presenter_set_h_scroll(de_gui_node_t* node, float val);

/**
* @brief Enables or disables vertical scroll for content. If scroll
* is enabled infinite bounds will be provided for content, so it can contain
* control of any size.
*/
void de_gui_scroll_content_presenter_enable_vertical_scroll(de_gui_node_t* n, bool value);

/**
* @brief Enables or disables horizontal scroll for content. If scroll
* is enabled infinite bounds will be provided for content, so it can contain
* control of any size.
*/
void de_gui_scroll_content_presenter_enable_horizontal_scroll(de_gui_node_t* n, bool value);