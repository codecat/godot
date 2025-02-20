/*************************************************************************/
/*  tile_map.cpp                                                         */
/*************************************************************************/
/*                       This file is part of:                           */
/*                           GODOT ENGINE                                */
/*                      https://godotengine.org                          */
/*************************************************************************/
/* Copyright (c) 2007-2021 Juan Linietsky, Ariel Manzur.                 */
/* Copyright (c) 2014-2021 Godot Engine contributors (cf. AUTHORS.md).   */
/*                                                                       */
/* Permission is hereby granted, free of charge, to any person obtaining */
/* a copy of this software and associated documentation files (the       */
/* "Software"), to deal in the Software without restriction, including   */
/* without limitation the rights to use, copy, modify, merge, publish,   */
/* distribute, sublicense, and/or sell copies of the Software, and to    */
/* permit persons to whom the Software is furnished to do so, subject to */
/* the following conditions:                                             */
/*                                                                       */
/* The above copyright notice and this permission notice shall be        */
/* included in all copies or substantial portions of the Software.       */
/*                                                                       */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,       */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF    */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.*/
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY  */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,  */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE     */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                */
/*************************************************************************/

#include "tile_map.h"

#include "core/io/marshalls.h"
#include "core/math/geometry_2d.h"
#include "core/os/os.h"

#include "servers/navigation_server_2d.h"

void TileMapPattern::set_cell(const Vector2i &p_coords, int p_source_id, const Vector2i p_atlas_coords, int p_alternative_tile) {
	ERR_FAIL_COND_MSG(p_coords.x < 0 || p_coords.y < 0, vformat("Cannot set cell with negative coords in a TileMapPattern. Wrong coords: %s", p_coords));

	size = size.max(p_coords + Vector2i(1, 1));
	pattern[p_coords] = TileMapCell(p_source_id, p_atlas_coords, p_alternative_tile);
}

bool TileMapPattern::has_cell(const Vector2i &p_coords) const {
	return pattern.has(p_coords);
}

void TileMapPattern::remove_cell(const Vector2i &p_coords, bool p_update_size) {
	ERR_FAIL_COND(!pattern.has(p_coords));

	pattern.erase(p_coords);
	if (p_update_size) {
		size = Vector2i();
		for (Map<Vector2i, TileMapCell>::Element *E = pattern.front(); E; E = E->next()) {
			size = size.max(E->key() + Vector2i(1, 1));
		}
	}
}

int TileMapPattern::get_cell_source_id(const Vector2i &p_coords) const {
	ERR_FAIL_COND_V(!pattern.has(p_coords), TileSet::INVALID_SOURCE);

	return pattern[p_coords].source_id;
}

Vector2i TileMapPattern::get_cell_atlas_coords(const Vector2i &p_coords) const {
	ERR_FAIL_COND_V(!pattern.has(p_coords), TileSetSource::INVALID_ATLAS_COORDS);

	return pattern[p_coords].get_atlas_coords();
}

int TileMapPattern::get_cell_alternative_tile(const Vector2i &p_coords) const {
	ERR_FAIL_COND_V(!pattern.has(p_coords), TileSetSource::INVALID_TILE_ALTERNATIVE);

	return pattern[p_coords].alternative_tile;
}

TypedArray<Vector2i> TileMapPattern::get_used_cells() const {
	// Returns the cells used in the tilemap.
	TypedArray<Vector2i> a;
	a.resize(pattern.size());
	int i = 0;
	for (Map<Vector2i, TileMapCell>::Element *E = pattern.front(); E; E = E->next()) {
		Vector2i p(E->key().x, E->key().y);
		a[i++] = p;
	}

	return a;
}

Vector2i TileMapPattern::get_size() const {
	return size;
}

void TileMapPattern::set_size(const Vector2i &p_size) {
	for (Map<Vector2i, TileMapCell>::Element *E = pattern.front(); E; E = E->next()) {
		Vector2i coords = E->key();
		if (p_size.x <= coords.x || p_size.y <= coords.y) {
			ERR_FAIL_MSG(vformat("Cannot set pattern size to %s, it contains a tile at %s. Size can only be increased.", p_size, coords));
		};
	}

	size = p_size;
}

bool TileMapPattern::is_empty() const {
	return pattern.is_empty();
};

void TileMapPattern::clear() {
	size = Vector2i();
	pattern.clear();
};

void TileMapPattern::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_cell", "coords", "source_id", "atlas_coords", "alternative_tile"), &TileMapPattern::set_cell, DEFVAL(TileSet::INVALID_SOURCE), DEFVAL(TileSetSource::INVALID_ATLAS_COORDS), DEFVAL(TileSetSource::INVALID_TILE_ALTERNATIVE));
	ClassDB::bind_method(D_METHOD("has_cell", "coords"), &TileMapPattern::has_cell);
	ClassDB::bind_method(D_METHOD("remove_cell", "coords"), &TileMapPattern::remove_cell);
	ClassDB::bind_method(D_METHOD("get_cell_source_id", "coords"), &TileMapPattern::get_cell_source_id);
	ClassDB::bind_method(D_METHOD("get_cell_atlas_coords", "coords"), &TileMapPattern::get_cell_atlas_coords);
	ClassDB::bind_method(D_METHOD("get_cell_alternative_tile", "coords"), &TileMapPattern::get_cell_alternative_tile);

	ClassDB::bind_method(D_METHOD("get_used_cells"), &TileMapPattern::get_used_cells);
	ClassDB::bind_method(D_METHOD("get_size"), &TileMapPattern::get_size);
	ClassDB::bind_method(D_METHOD("set_size", "size"), &TileMapPattern::set_size);
	ClassDB::bind_method(D_METHOD("is_empty"), &TileMapPattern::is_empty);
}

Vector2i TileMap::transform_coords_layout(Vector2i p_coords, TileSet::TileOffsetAxis p_offset_axis, TileSet::TileLayout p_from_layout, TileSet::TileLayout p_to_layout) {
	// Transform to stacked layout.
	Vector2i output = p_coords;
	if (p_offset_axis == TileSet::TILE_OFFSET_AXIS_VERTICAL) {
		SWAP(output.x, output.y);
	}
	switch (p_from_layout) {
		case TileSet::TILE_LAYOUT_STACKED:
			break;
		case TileSet::TILE_LAYOUT_STACKED_OFFSET:
			if (output.y % 2) {
				output.x -= 1;
			}
			break;
		case TileSet::TILE_LAYOUT_STAIRS_RIGHT:
		case TileSet::TILE_LAYOUT_STAIRS_DOWN:
			if ((p_from_layout == TileSet::TILE_LAYOUT_STAIRS_RIGHT) ^ (p_offset_axis == TileSet::TILE_OFFSET_AXIS_VERTICAL)) {
				if (output.y < 0 && bool(output.y % 2)) {
					output = Vector2i(output.x + output.y / 2 - 1, output.y);
				} else {
					output = Vector2i(output.x + output.y / 2, output.y);
				}
			} else {
				if (output.x < 0 && bool(output.x % 2)) {
					output = Vector2i(output.x / 2 - 1, output.x + output.y * 2);
				} else {
					output = Vector2i(output.x / 2, output.x + output.y * 2);
				}
			}
			break;
		case TileSet::TILE_LAYOUT_DIAMOND_RIGHT:
		case TileSet::TILE_LAYOUT_DIAMOND_DOWN:
			if ((p_from_layout == TileSet::TILE_LAYOUT_DIAMOND_RIGHT) ^ (p_offset_axis == TileSet::TILE_OFFSET_AXIS_VERTICAL)) {
				if ((output.x + output.y) < 0 && (output.x - output.y) % 2) {
					output = Vector2i((output.x + output.y) / 2 - 1, output.y - output.x);
				} else {
					output = Vector2i((output.x + output.y) / 2, -output.x + output.y);
				}
			} else {
				if ((output.x - output.y) < 0 && (output.x + output.y) % 2) {
					output = Vector2i((output.x - output.y) / 2 - 1, output.x + output.y);
				} else {
					output = Vector2i((output.x - output.y) / 2, output.x + output.y);
				}
			}
			break;
	}

	switch (p_to_layout) {
		case TileSet::TILE_LAYOUT_STACKED:
			break;
		case TileSet::TILE_LAYOUT_STACKED_OFFSET:
			if (output.y % 2) {
				output.x += 1;
			}
			break;
		case TileSet::TILE_LAYOUT_STAIRS_RIGHT:
		case TileSet::TILE_LAYOUT_STAIRS_DOWN:
			if ((p_to_layout == TileSet::TILE_LAYOUT_STAIRS_RIGHT) ^ (p_offset_axis == TileSet::TILE_OFFSET_AXIS_VERTICAL)) {
				if (output.y < 0 && (output.y % 2)) {
					output = Vector2i(output.x - output.y / 2 + 1, output.y);
				} else {
					output = Vector2i(output.x - output.y / 2, output.y);
				}
			} else {
				if (output.y % 2) {
					if (output.y < 0) {
						output = Vector2i(2 * output.x + 1, -output.x + output.y / 2 - 1);
					} else {
						output = Vector2i(2 * output.x + 1, -output.x + output.y / 2);
					}
				} else {
					output = Vector2i(2 * output.x, -output.x + output.y / 2);
				}
			}
			break;
		case TileSet::TILE_LAYOUT_DIAMOND_RIGHT:
		case TileSet::TILE_LAYOUT_DIAMOND_DOWN:
			if ((p_to_layout == TileSet::TILE_LAYOUT_DIAMOND_RIGHT) ^ (p_offset_axis == TileSet::TILE_OFFSET_AXIS_VERTICAL)) {
				if (output.y % 2) {
					if (output.y > 0) {
						output = Vector2i(output.x - output.y / 2, output.x + output.y / 2 + 1);
					} else {
						output = Vector2i(output.x - output.y / 2 + 1, output.x + output.y / 2);
					}
				} else {
					output = Vector2i(output.x - output.y / 2, output.x + output.y / 2);
				}
			} else {
				if (output.y % 2) {
					if (output.y < 0) {
						output = Vector2i(output.x + output.y / 2, -output.x + output.y / 2 - 1);
					} else {
						output = Vector2i(output.x + output.y / 2 + 1, -output.x + output.y / 2);
					}
				} else {
					output = Vector2i(output.x + output.y / 2, -output.x + output.y / 2);
				}
			}
			break;
	}

	if (p_offset_axis == TileSet::TILE_OFFSET_AXIS_VERTICAL) {
		SWAP(output.x, output.y);
	}

	return output;
}

int TileMap::get_effective_quadrant_size(int p_layer) const {
	// When using YSort, the quadrant size is reduced to 1 to have one CanvasItem per quadrant
	if (is_y_sort_enabled() && layers[p_layer].y_sort_enabled) {
		return 1;
	} else {
		return quadrant_size;
	}
}

void TileMap::set_selected_layer(int p_layer_id) {
	ERR_FAIL_COND(p_layer_id < -1 || p_layer_id >= (int)layers.size());
	selected_layer = p_layer_id;
	emit_signal(SNAME("changed"));
	_make_all_quadrants_dirty();
}

int TileMap::get_selected_layer() const {
	return selected_layer;
}

void TileMap::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			pending_update = true;
			_recreate_internals();
		} break;
		case NOTIFICATION_EXIT_TREE: {
			_clear_internals();
		} break;
	}

	// Transfers the notification to tileset plugins.
	if (tile_set.is_valid()) {
		_rendering_notification(p_what);
		_physics_notification(p_what);
		_navigation_notification(p_what);
	}
}

Ref<TileSet> TileMap::get_tileset() const {
	return tile_set;
}

void TileMap::set_tileset(const Ref<TileSet> &p_tileset) {
	if (p_tileset == tile_set) {
		return;
	}

	// Set the tileset, registering to its changes.
	if (tile_set.is_valid()) {
		tile_set->disconnect("changed", callable_mp(this, &TileMap::_tile_set_changed));
	}

	if (!p_tileset.is_valid()) {
		_clear_internals();
	}

	tile_set = p_tileset;

	if (tile_set.is_valid()) {
		tile_set->connect("changed", callable_mp(this, &TileMap::_tile_set_changed));
		_recreate_internals();
	}

	emit_signal(SNAME("changed"));
}

void TileMap::set_quadrant_size(int p_size) {
	ERR_FAIL_COND_MSG(p_size < 1, "TileMapQuadrant size cannot be smaller than 1.");

	quadrant_size = p_size;
	_recreate_internals();
	emit_signal(SNAME("changed"));
}

int TileMap::get_quadrant_size() const {
	return quadrant_size;
}

void TileMap::set_layers_count(int p_layers_count) {
	ERR_FAIL_COND(p_layers_count < 0);
	_clear_internals();

	layers.resize(p_layers_count);
	_recreate_internals();
	notify_property_list_changed();

	if (selected_layer >= p_layers_count) {
		selected_layer = -1;
	}

	emit_signal(SNAME("changed"));

	update_configuration_warnings();
}

int TileMap::get_layers_count() const {
	return layers.size();
}

void TileMap::set_layer_name(int p_layer, String p_name) {
	ERR_FAIL_INDEX(p_layer, (int)layers.size());
	layers[p_layer].name = p_name;
	emit_signal(SNAME("changed"));
}

String TileMap::get_layer_name(int p_layer) const {
	ERR_FAIL_INDEX_V(p_layer, (int)layers.size(), String());
	return layers[p_layer].name;
}

void TileMap::set_layer_enabled(int p_layer, bool p_enabled) {
	ERR_FAIL_INDEX(p_layer, (int)layers.size());
	layers[p_layer].enabled = p_enabled;
	_recreate_internals();
	emit_signal(SNAME("changed"));

	update_configuration_warnings();
}

bool TileMap::is_layer_enabled(int p_layer) const {
	ERR_FAIL_INDEX_V(p_layer, (int)layers.size(), false);
	return layers[p_layer].enabled;
}

void TileMap::set_layer_y_sort_enabled(int p_layer, bool p_y_sort_enabled) {
	ERR_FAIL_INDEX(p_layer, (int)layers.size());
	layers[p_layer].y_sort_enabled = p_y_sort_enabled;
	_recreate_internals();
	emit_signal(SNAME("changed"));

	update_configuration_warnings();
}

bool TileMap::is_layer_y_sort_enabled(int p_layer) const {
	ERR_FAIL_INDEX_V(p_layer, (int)layers.size(), false);
	return layers[p_layer].y_sort_enabled;
}

void TileMap::set_layer_y_sort_origin(int p_layer, int p_y_sort_origin) {
	ERR_FAIL_INDEX(p_layer, (int)layers.size());
	layers[p_layer].y_sort_origin = p_y_sort_origin;
	_recreate_internals();
	emit_signal(SNAME("changed"));
}

int TileMap::get_layer_y_sort_origin(int p_layer) const {
	ERR_FAIL_INDEX_V(p_layer, (int)layers.size(), false);
	return layers[p_layer].y_sort_origin;
}

void TileMap::set_layer_z_index(int p_layer, int p_z_index) {
	ERR_FAIL_INDEX(p_layer, (int)layers.size());
	layers[p_layer].z_index = p_z_index;
	_recreate_internals();
	emit_signal(SNAME("changed"));

	update_configuration_warnings();
}

int TileMap::get_layer_z_index(int p_layer) const {
	ERR_FAIL_INDEX_V(p_layer, (int)layers.size(), false);
	return layers[p_layer].z_index;
}

void TileMap::set_collision_visibility_mode(TileMap::VisibilityMode p_show_collision) {
	collision_visibility_mode = p_show_collision;
	_recreate_internals();
	emit_signal(SNAME("changed"));
}

TileMap::VisibilityMode TileMap::get_collision_visibility_mode() {
	return collision_visibility_mode;
}

void TileMap::set_navigation_visibility_mode(TileMap::VisibilityMode p_show_navigation) {
	navigation_visibility_mode = p_show_navigation;
	_recreate_internals();
	emit_signal(SNAME("changed"));
}

TileMap::VisibilityMode TileMap::get_navigation_visibility_mode() {
	return navigation_visibility_mode;
}

void TileMap::set_y_sort_enabled(bool p_enable) {
	Node2D::set_y_sort_enabled(p_enable);
	_recreate_internals();
	emit_signal(SNAME("changed"));
}

Vector2i TileMap::_coords_to_quadrant_coords(int p_layer, const Vector2i &p_coords) const {
	int quadrant_size = get_effective_quadrant_size(p_layer);

	// Rounding down, instead of simply rounding towards zero (truncating)
	return Vector2i(
			p_coords.x > 0 ? p_coords.x / quadrant_size : (p_coords.x - (quadrant_size - 1)) / quadrant_size,
			p_coords.y > 0 ? p_coords.y / quadrant_size : (p_coords.y - (quadrant_size - 1)) / quadrant_size);
}

Map<Vector2i, TileMapQuadrant>::Element *TileMap::_create_quadrant(int p_layer, const Vector2i &p_qk) {
	ERR_FAIL_INDEX_V(p_layer, (int)layers.size(), nullptr);

	TileMapQuadrant q;
	q.layer = p_layer;
	q.coords = p_qk;

	rect_cache_dirty = true;

	// Create the debug canvas item.
	RenderingServer *rs = RenderingServer::get_singleton();
	q.debug_canvas_item = rs->canvas_item_create();
	rs->canvas_item_set_z_index(q.debug_canvas_item, RS::CANVAS_ITEM_Z_MAX - 1);
	rs->canvas_item_set_parent(q.debug_canvas_item, get_canvas_item());

	// Call the create_quadrant method on plugins
	if (tile_set.is_valid()) {
		_rendering_create_quadrant(&q);
		_physics_create_quadrant(&q);
	}

	return layers[p_layer].quadrant_map.insert(p_qk, q);
}

void TileMap::_make_quadrant_dirty(Map<Vector2i, TileMapQuadrant>::Element *Q) {
	// Make the given quadrant dirty, then trigger an update later.
	TileMapQuadrant &q = Q->get();
	if (!q.dirty_list_element.in_list()) {
		layers[q.layer].dirty_quadrant_list.add(&q.dirty_list_element);
	}
	_queue_update_dirty_quadrants();
}

void TileMap::_make_all_quadrants_dirty() {
	// Make all quandrants dirty, then trigger an update later.
	for (unsigned int layer = 0; layer < layers.size(); layer++) {
		for (Map<Vector2i, TileMapQuadrant>::Element *E = layers[layer].quadrant_map.front(); E; E = E->next()) {
			if (!E->value().dirty_list_element.in_list()) {
				layers[layer].dirty_quadrant_list.add(&E->value().dirty_list_element);
			}
		}
	}
	_queue_update_dirty_quadrants();
}

void TileMap::_queue_update_dirty_quadrants() {
	if (pending_update || !is_inside_tree()) {
		return;
	}
	pending_update = true;
	call_deferred(SNAME("_update_dirty_quadrants"));
}

void TileMap::_update_dirty_quadrants() {
	if (!pending_update) {
		return;
	}
	if (!is_inside_tree() || !tile_set.is_valid()) {
		pending_update = false;
		return;
	}

	for (unsigned int layer = 0; layer < layers.size(); layer++) {
		// Update the coords cache.
		for (SelfList<TileMapQuadrant> *q = layers[layer].dirty_quadrant_list.first(); q; q = q->next()) {
			q->self()->map_to_world.clear();
			q->self()->world_to_map.clear();
			for (Set<Vector2i>::Element *E = q->self()->cells.front(); E; E = E->next()) {
				Vector2i pk = E->get();
				Vector2i pk_world_coords = map_to_world(pk);
				q->self()->map_to_world[pk] = pk_world_coords;
				q->self()->world_to_map[pk_world_coords] = pk;
			}
		}

		// Call the update_dirty_quadrant method on plugins.
		_rendering_update_dirty_quadrants(layers[layer].dirty_quadrant_list);
		_physics_update_dirty_quadrants(layers[layer].dirty_quadrant_list);
		_navigation_update_dirty_quadrants(layers[layer].dirty_quadrant_list);
		_scenes_update_dirty_quadrants(layers[layer].dirty_quadrant_list);

		// Redraw the debug canvas_items.
		RenderingServer *rs = RenderingServer::get_singleton();
		for (SelfList<TileMapQuadrant> *q = layers[layer].dirty_quadrant_list.first(); q; q = q->next()) {
			rs->canvas_item_clear(q->self()->debug_canvas_item);
			Transform2D xform;
			xform.set_origin(map_to_world(q->self()->coords * get_effective_quadrant_size(layer)));
			rs->canvas_item_set_transform(q->self()->debug_canvas_item, xform);

			_rendering_draw_quadrant_debug(q->self());
			_physics_draw_quadrant_debug(q->self());
			_navigation_draw_quadrant_debug(q->self());
			_scenes_draw_quadrant_debug(q->self());
		}

		// Clear the list
		while (layers[layer].dirty_quadrant_list.first()) {
			layers[layer].dirty_quadrant_list.remove(layers[layer].dirty_quadrant_list.first());
		}
	}

	pending_update = false;

	_recompute_rect_cache();
}

void TileMap::_recreate_internals() {
	// Clear all internals.
	_clear_internals();

	for (unsigned int layer = 0; layer < layers.size(); layer++) {
		if (!layers[layer].enabled) {
			continue;
		}

		// Upadate the layer internals.
		_rendering_update_layer(layer);

		// Recreate the quadrants.
		const Map<Vector2i, TileMapCell> &tile_map = layers[layer].tile_map;
		for (Map<Vector2i, TileMapCell>::Element *E = tile_map.front(); E; E = E->next()) {
			Vector2i qk = _coords_to_quadrant_coords(layer, Vector2i(E->key().x, E->key().y));

			Map<Vector2i, TileMapQuadrant>::Element *Q = layers[layer].quadrant_map.find(qk);
			if (!Q) {
				Q = _create_quadrant(layer, qk);
				layers[layer].dirty_quadrant_list.add(&Q->get().dirty_list_element);
			}

			Vector2i pk = E->key();
			Q->get().cells.insert(pk);

			_make_quadrant_dirty(Q);
		}
	}

	_update_dirty_quadrants();
}

void TileMap::_erase_quadrant(Map<Vector2i, TileMapQuadrant>::Element *Q) {
	// Remove a quadrant.
	TileMapQuadrant *q = &(Q->get());

	// Call the cleanup_quadrant method on plugins.
	if (tile_set.is_valid()) {
		_rendering_cleanup_quadrant(q);
		_physics_cleanup_quadrant(q);
		_navigation_cleanup_quadrant(q);
		_scenes_cleanup_quadrant(q);
	}

	// Remove the quadrant from the dirty_list if it is there.
	if (q->dirty_list_element.in_list()) {
		layers[q->layer].dirty_quadrant_list.remove(&(q->dirty_list_element));
	}

	// Free the debug canvas item.
	RenderingServer *rs = RenderingServer::get_singleton();
	rs->free(q->debug_canvas_item);

	layers[q->layer].quadrant_map.erase(Q);
	rect_cache_dirty = true;
}

void TileMap::_clear_layer_internals(int p_layer) {
	ERR_FAIL_INDEX(p_layer, (int)layers.size());

	// Clear quadrants.
	while (layers[p_layer].quadrant_map.size()) {
		_erase_quadrant(layers[p_layer].quadrant_map.front());
	}

	// Clear the layers internals.
	_rendering_cleanup_layer(p_layer);

	// Clear the dirty quadrants list.
	while (layers[p_layer].dirty_quadrant_list.first()) {
		layers[p_layer].dirty_quadrant_list.remove(layers[p_layer].dirty_quadrant_list.first());
	}
}

void TileMap::_clear_internals() {
	// Clear quadrants.
	for (unsigned int layer = 0; layer < layers.size(); layer++) {
		_clear_layer_internals(layer);
	}
}

void TileMap::_recompute_rect_cache() {
	// Compute the displayed area of the tilemap.
#ifdef DEBUG_ENABLED

	if (!rect_cache_dirty) {
		return;
	}

	Rect2 r_total;
	for (unsigned int layer = 0; layer < layers.size(); layer++) {
		for (Map<Vector2i, TileMapQuadrant>::Element *E = layers[layer].quadrant_map.front(); E; E = E->next()) {
			Rect2 r;
			r.position = map_to_world(E->key() * get_effective_quadrant_size(layer));
			r.expand_to(map_to_world((E->key() + Vector2i(1, 0)) * get_effective_quadrant_size(layer)));
			r.expand_to(map_to_world((E->key() + Vector2i(1, 1)) * get_effective_quadrant_size(layer)));
			r.expand_to(map_to_world((E->key() + Vector2i(0, 1)) * get_effective_quadrant_size(layer)));
			if (E == layers[layer].quadrant_map.front()) {
				r_total = r;
			} else {
				r_total = r_total.merge(r);
			}
		}
	}

	rect_cache = r_total;

	item_rect_changed();

	rect_cache_dirty = false;
#endif
}

/////////////////////////////// Rendering //////////////////////////////////////

void TileMap::_rendering_notification(int p_what) {
	switch (p_what) {
		case CanvasItem::NOTIFICATION_VISIBILITY_CHANGED: {
			bool visible = is_visible_in_tree();
			for (int layer = 0; layer < (int)layers.size(); layer++) {
				for (Map<Vector2i, TileMapQuadrant>::Element *E_quadrant = layers[layer].quadrant_map.front(); E_quadrant; E_quadrant = E_quadrant->next()) {
					TileMapQuadrant &q = E_quadrant->get();

					// Update occluders transform.
					for (Map<Vector2i, Vector2i, TileMapQuadrant::CoordsWorldComparator>::Element *E_cell = q.world_to_map.front(); E_cell; E_cell = E_cell->next()) {
						Transform2D xform;
						xform.set_origin(E_cell->key());
						for (const RID &occluder : q.occluders) {
							RS::get_singleton()->canvas_light_occluder_set_enabled(occluder, visible);
						}
					}
				}
			}
		} break;
		case CanvasItem::NOTIFICATION_TRANSFORM_CHANGED: {
			if (!is_inside_tree()) {
				return;
			}
			for (int layer = 0; layer < (int)layers.size(); layer++) {
				for (Map<Vector2i, TileMapQuadrant>::Element *E_quadrant = layers[layer].quadrant_map.front(); E_quadrant; E_quadrant = E_quadrant->next()) {
					TileMapQuadrant &q = E_quadrant->get();

					// Update occluders transform.
					for (Map<Vector2i, Vector2i, TileMapQuadrant::CoordsWorldComparator>::Element *E_cell = q.world_to_map.front(); E_cell; E_cell = E_cell->next()) {
						Transform2D xform;
						xform.set_origin(E_cell->key());
						for (const RID &occluder : q.occluders) {
							RS::get_singleton()->canvas_light_occluder_set_transform(occluder, get_global_transform() * xform);
						}
					}
				}
			}
		} break;
		case CanvasItem::NOTIFICATION_DRAW: {
			if (tile_set.is_valid()) {
				RenderingServer::get_singleton()->canvas_item_set_sort_children_by_y(get_canvas_item(), is_y_sort_enabled());
			}
		} break;
	}
}

void TileMap::_rendering_update_layer(int p_layer) {
	ERR_FAIL_INDEX(p_layer, (int)layers.size());

	RenderingServer *rs = RenderingServer::get_singleton();
	if (!layers[p_layer].canvas_item.is_valid()) {
		RID ci = rs->canvas_item_create();
		rs->canvas_item_set_parent(ci, get_canvas_item());

		/*Transform2D xform;
		xform.set_origin(Vector2(0, p_layer));
		rs->canvas_item_set_transform(ci, xform);*/
		rs->canvas_item_set_draw_index(ci, p_layer);

		layers[p_layer].canvas_item = ci;
	}
	RID &ci = layers[p_layer].canvas_item;
	rs->canvas_item_set_sort_children_by_y(ci, layers[p_layer].y_sort_enabled);
	rs->canvas_item_set_use_parent_material(ci, get_use_parent_material() || get_material().is_valid());
	rs->canvas_item_set_z_index(ci, layers[p_layer].z_index);
	rs->canvas_item_set_default_texture_filter(ci, RS::CanvasItemTextureFilter(get_texture_filter()));
	rs->canvas_item_set_default_texture_repeat(ci, RS::CanvasItemTextureRepeat(get_texture_repeat()));
	rs->canvas_item_set_light_mask(ci, get_light_mask());
}

void TileMap::_rendering_cleanup_layer(int p_layer) {
	ERR_FAIL_INDEX(p_layer, (int)layers.size());

	RenderingServer *rs = RenderingServer::get_singleton();
	if (!layers[p_layer].canvas_item.is_valid()) {
		rs->free(layers[p_layer].canvas_item);
	}
}

void TileMap::_rendering_update_dirty_quadrants(SelfList<TileMapQuadrant>::List &r_dirty_quadrant_list) {
	ERR_FAIL_COND(!is_inside_tree());
	ERR_FAIL_COND(!tile_set.is_valid());

	bool visible = is_visible_in_tree();

	SelfList<TileMapQuadrant> *q_list_element = r_dirty_quadrant_list.first();
	while (q_list_element) {
		TileMapQuadrant &q = *q_list_element->self();

		RenderingServer *rs = RenderingServer::get_singleton();

		// Free the canvas items.
		for (const RID &ci : q.canvas_items) {
			rs->free(ci);
		}
		q.canvas_items.clear();

		// Free the occluders.
		for (const RID &occluder : q.occluders) {
			rs->free(occluder);
		}
		q.occluders.clear();

		// Those allow to group cell per material or z-index.
		Ref<ShaderMaterial> prev_material;
		int prev_z_index = 0;
		RID prev_canvas_item;

		// Iterate over the cells of the quadrant.
		for (Map<Vector2i, Vector2i, TileMapQuadrant::CoordsWorldComparator>::Element *E_cell = q.world_to_map.front(); E_cell; E_cell = E_cell->next()) {
			TileMapCell c = get_cell(q.layer, E_cell->value(), true);

			TileSetSource *source;
			if (tile_set->has_source(c.source_id)) {
				source = *tile_set->get_source(c.source_id);

				if (!source->has_tile(c.get_atlas_coords()) || !source->has_alternative_tile(c.get_atlas_coords(), c.alternative_tile)) {
					continue;
				}

				TileSetAtlasSource *atlas_source = Object::cast_to<TileSetAtlasSource>(source);
				if (atlas_source) {
					// Get the tile data.
					TileData *tile_data = Object::cast_to<TileData>(atlas_source->get_tile_data(c.get_atlas_coords(), c.alternative_tile));
					Ref<ShaderMaterial> mat = tile_data->tile_get_material();
					int z_index = layers[q.layer].z_index + tile_data->get_z_index();

					// Quandrant pos.
					Vector2 position = map_to_world(q.coords * get_effective_quadrant_size(q.layer));
					if (is_y_sort_enabled() && layers[q.layer].y_sort_enabled) {
						// When Y-sorting, the quandrant size is sure to be 1, we can thus offset the CanvasItem.
						position.y += layers[q.layer].y_sort_origin + tile_data->get_y_sort_origin();
					}

					// --- CanvasItems ---
					// Create two canvas items, for rendering and debug.
					RID canvas_item;

					// Check if the material or the z_index changed.
					if (prev_canvas_item == RID() || prev_material != mat || prev_z_index != z_index) {
						// If so, create a new CanvasItem.
						canvas_item = rs->canvas_item_create();
						if (mat.is_valid()) {
							rs->canvas_item_set_material(canvas_item, mat->get_rid());
						}
						rs->canvas_item_set_parent(canvas_item, layers[q.layer].canvas_item);
						rs->canvas_item_set_use_parent_material(canvas_item, get_use_parent_material() || get_material().is_valid());

						Transform2D xform;
						xform.set_origin(position);
						rs->canvas_item_set_transform(canvas_item, xform);

						rs->canvas_item_set_light_mask(canvas_item, get_light_mask());
						rs->canvas_item_set_z_index(canvas_item, z_index);

						rs->canvas_item_set_default_texture_filter(canvas_item, RS::CanvasItemTextureFilter(get_texture_filter()));
						rs->canvas_item_set_default_texture_repeat(canvas_item, RS::CanvasItemTextureRepeat(get_texture_repeat()));

						q.canvas_items.push_back(canvas_item);

						prev_canvas_item = canvas_item;
						prev_material = mat;
						prev_z_index = z_index;

					} else {
						// Keep the same canvas_item to draw on.
						canvas_item = prev_canvas_item;
					}

					// Drawing the tile in the canvas item.
					Color modulate = get_self_modulate();
					if (selected_layer >= 0) {
						if (q.layer < selected_layer) {
							modulate = modulate.darkened(0.5);
						} else if (q.layer > selected_layer) {
							modulate = modulate.darkened(0.5);
							modulate.a *= 0.3;
						}
					}
					draw_tile(canvas_item, E_cell->key() - position, tile_set, c.source_id, c.get_atlas_coords(), c.alternative_tile, modulate);

					// --- Occluders ---
					for (int i = 0; i < tile_set->get_occlusion_layers_count(); i++) {
						Transform2D xform;
						xform.set_origin(E_cell->key());
						if (tile_data->get_occluder(i).is_valid()) {
							RID occluder_id = rs->canvas_light_occluder_create();
							rs->canvas_light_occluder_set_enabled(occluder_id, visible);
							rs->canvas_light_occluder_set_transform(occluder_id, get_global_transform() * xform);
							rs->canvas_light_occluder_set_polygon(occluder_id, tile_data->get_occluder(i)->get_rid());
							rs->canvas_light_occluder_attach_to_canvas(occluder_id, get_canvas());
							rs->canvas_light_occluder_set_light_mask(occluder_id, tile_set->get_occlusion_layer_light_mask(i));
							q.occluders.push_back(occluder_id);
						}
					}
				}
			}
		}

		_rendering_quadrant_order_dirty = true;
		q_list_element = q_list_element->next();
	}

	// Reset the drawing indices
	if (_rendering_quadrant_order_dirty) {
		int index = -(int64_t)0x80000000; //always must be drawn below children.

		for (int layer = 0; layer < (int)layers.size(); layer++) {
			// Sort the quadrants coords per world coordinates
			Map<Vector2i, Vector2i, TileMapQuadrant::CoordsWorldComparator> world_to_map;
			for (Map<Vector2i, TileMapQuadrant>::Element *E = layers[layer].quadrant_map.front(); E; E = E->next()) {
				world_to_map[map_to_world(E->key())] = E->key();
			}

			// Sort the quadrants
			for (Map<Vector2i, Vector2i, TileMapQuadrant::CoordsWorldComparator>::Element *E = world_to_map.front(); E; E = E->next()) {
				TileMapQuadrant &q = layers[layer].quadrant_map[E->value()];
				for (const RID &ci : q.canvas_items) {
					RS::get_singleton()->canvas_item_set_draw_index(ci, index++);
				}
			}
		}
		_rendering_quadrant_order_dirty = false;
	}
}

void TileMap::_rendering_create_quadrant(TileMapQuadrant *p_quadrant) {
	ERR_FAIL_COND(!tile_set.is_valid());

	_rendering_quadrant_order_dirty = true;
}

void TileMap::_rendering_cleanup_quadrant(TileMapQuadrant *p_quadrant) {
	// Free the canvas items.
	for (const RID &ci : p_quadrant->canvas_items) {
		RenderingServer::get_singleton()->free(ci);
	}
	p_quadrant->canvas_items.clear();

	// Free the occluders.
	for (const RID &occluder : p_quadrant->occluders) {
		RenderingServer::get_singleton()->free(occluder);
	}
	p_quadrant->occluders.clear();
}

void TileMap::_rendering_draw_quadrant_debug(TileMapQuadrant *p_quadrant) {
	ERR_FAIL_COND(!tile_set.is_valid());

	if (!Engine::get_singleton()->is_editor_hint()) {
		return;
	}

	// Draw a placeholder for scenes needing one.
	RenderingServer *rs = RenderingServer::get_singleton();
	Vector2 quadrant_pos = map_to_world(p_quadrant->coords * get_effective_quadrant_size(p_quadrant->layer));
	for (Set<Vector2i>::Element *E_cell = p_quadrant->cells.front(); E_cell; E_cell = E_cell->next()) {
		const TileMapCell &c = get_cell(p_quadrant->layer, E_cell->get(), true);

		TileSetSource *source;
		if (tile_set->has_source(c.source_id)) {
			source = *tile_set->get_source(c.source_id);

			if (!source->has_tile(c.get_atlas_coords()) || !source->has_alternative_tile(c.get_atlas_coords(), c.alternative_tile)) {
				continue;
			}

			TileSetAtlasSource *atlas_source = Object::cast_to<TileSetAtlasSource>(source);
			if (atlas_source) {
				Vector2i grid_size = atlas_source->get_atlas_grid_size();
				if (!atlas_source->get_texture().is_valid() || c.get_atlas_coords().x >= grid_size.x || c.get_atlas_coords().y >= grid_size.y) {
					// Generate a random color from the hashed values of the tiles.
					Array to_hash;
					to_hash.push_back(c.source_id);
					to_hash.push_back(c.get_atlas_coords());
					to_hash.push_back(c.alternative_tile);
					uint32_t hash = RandomPCG(to_hash.hash()).rand();

					Color color;
					color = color.from_hsv(
							(float)((hash >> 24) & 0xFF) / 256.0,
							Math::lerp(0.5, 1.0, (float)((hash >> 16) & 0xFF) / 256.0),
							Math::lerp(0.5, 1.0, (float)((hash >> 8) & 0xFF) / 256.0),
							0.8);

					// Draw a placeholder tile.
					Transform2D xform;
					xform.set_origin(map_to_world(E_cell->get()) - quadrant_pos);
					rs->canvas_item_add_set_transform(p_quadrant->debug_canvas_item, xform);
					rs->canvas_item_add_circle(p_quadrant->debug_canvas_item, Vector2(), MIN(tile_set->get_tile_size().x, tile_set->get_tile_size().y) / 4.0, color);
				}
			}
		}
	}
}

void TileMap::draw_tile(RID p_canvas_item, Vector2i p_position, const Ref<TileSet> p_tile_set, int p_atlas_source_id, Vector2i p_atlas_coords, int p_alternative_tile, Color p_modulation) {
	ERR_FAIL_COND(!p_tile_set.is_valid());
	ERR_FAIL_COND(!p_tile_set->has_source(p_atlas_source_id));
	ERR_FAIL_COND(!p_tile_set->get_source(p_atlas_source_id)->has_tile(p_atlas_coords));
	ERR_FAIL_COND(!p_tile_set->get_source(p_atlas_source_id)->has_alternative_tile(p_atlas_coords, p_alternative_tile));

	TileSetSource *source = *p_tile_set->get_source(p_atlas_source_id);
	TileSetAtlasSource *atlas_source = Object::cast_to<TileSetAtlasSource>(source);
	if (atlas_source) {
		// Get the texture.
		Ref<Texture2D> tex = atlas_source->get_texture();
		if (!tex.is_valid()) {
			return;
		}

		// Check if we are in the texture, return otherwise.
		Vector2i grid_size = atlas_source->get_atlas_grid_size();
		if (p_atlas_coords.x >= grid_size.x || p_atlas_coords.y >= grid_size.y) {
			return;
		}

		// Get tile data.
		TileData *tile_data = Object::cast_to<TileData>(atlas_source->get_tile_data(p_atlas_coords, p_alternative_tile));

		// Compute the offset
		Rect2i source_rect = atlas_source->get_tile_texture_region(p_atlas_coords);
		Vector2i tile_offset = atlas_source->get_tile_effective_texture_offset(p_atlas_coords, p_alternative_tile);

		// Compute the destination rectangle in the CanvasItem.
		Rect2 dest_rect;
		dest_rect.size = source_rect.size;
		dest_rect.size.x += FP_ADJUST;
		dest_rect.size.y += FP_ADJUST;

		bool transpose = tile_data->get_transpose();
		if (transpose) {
			dest_rect.position = (p_position - Vector2(dest_rect.size.y, dest_rect.size.x) / 2 - tile_offset);
		} else {
			dest_rect.position = (p_position - dest_rect.size / 2 - tile_offset);
		}

		if (tile_data->get_flip_h()) {
			dest_rect.size.x = -dest_rect.size.x;
		}

		if (tile_data->get_flip_v()) {
			dest_rect.size.y = -dest_rect.size.y;
		}

		// Get the tile modulation.
		Color modulate = tile_data->get_modulate();
		modulate = Color(modulate.r * p_modulation.r, modulate.g * p_modulation.g, modulate.b * p_modulation.b, modulate.a * p_modulation.a);

		// Draw the tile.
		tex->draw_rect_region(p_canvas_item, dest_rect, source_rect, modulate, transpose, p_tile_set->is_uv_clipping());
	}
}

/////////////////////////////// Physics //////////////////////////////////////

void TileMap::_physics_notification(int p_what) {
	switch (p_what) {
		case CanvasItem::NOTIFICATION_TRANSFORM_CHANGED: {
			// Update the bodies transforms.
			if (is_inside_tree()) {
				for (int layer = 0; layer < (int)layers.size(); layer++) {
					Transform2D global_transform = get_global_transform();

					for (Map<Vector2i, TileMapQuadrant>::Element *E = layers[layer].quadrant_map.front(); E; E = E->next()) {
						TileMapQuadrant &q = E->get();

						Transform2D xform;
						xform.set_origin(map_to_world(E->key() * get_effective_quadrant_size(layer)));
						xform = global_transform * xform;

						for (int body_index = 0; body_index < q.bodies.size(); body_index++) {
							PhysicsServer2D::get_singleton()->body_set_state(q.bodies[body_index], PhysicsServer2D::BODY_STATE_TRANSFORM, xform);
						}
					}
				}
			}
		} break;
	}
}

void TileMap::_physics_update_dirty_quadrants(SelfList<TileMapQuadrant>::List &r_dirty_quadrant_list) {
	ERR_FAIL_COND(!is_inside_tree());
	ERR_FAIL_COND(!tile_set.is_valid());

	Transform2D global_transform = get_global_transform();
	PhysicsServer2D *ps = PhysicsServer2D::get_singleton();

	SelfList<TileMapQuadrant> *q_list_element = r_dirty_quadrant_list.first();
	while (q_list_element) {
		TileMapQuadrant &q = *q_list_element->self();

		Vector2 quadrant_pos = map_to_world(q.coords * get_effective_quadrant_size(q.layer));

		// Clear shapes.
		for (int body_index = 0; body_index < q.bodies.size(); body_index++) {
			ps->body_clear_shapes(q.bodies[body_index]);

			// Position the bodies.
			Transform2D xform;
			xform.set_origin(quadrant_pos);
			xform = global_transform * xform;
			ps->body_set_state(q.bodies[body_index], PhysicsServer2D::BODY_STATE_TRANSFORM, xform);
		}

		for (Set<Vector2i>::Element *E_cell = q.cells.front(); E_cell; E_cell = E_cell->next()) {
			TileMapCell c = get_cell(q.layer, E_cell->get(), true);

			TileSetSource *source;
			if (tile_set->has_source(c.source_id)) {
				source = *tile_set->get_source(c.source_id);

				if (!source->has_tile(c.get_atlas_coords()) || !source->has_alternative_tile(c.get_atlas_coords(), c.alternative_tile)) {
					continue;
				}

				TileSetAtlasSource *atlas_source = Object::cast_to<TileSetAtlasSource>(source);
				if (atlas_source) {
					TileData *tile_data = Object::cast_to<TileData>(atlas_source->get_tile_data(c.get_atlas_coords(), c.alternative_tile));

					for (int body_index = 0; body_index < q.bodies.size(); body_index++) {
						// Add the shapes again.
						for (int polygon_index = 0; polygon_index < tile_data->get_collision_polygons_count(body_index); polygon_index++) {
							bool one_way_collision = tile_data->is_collision_polygon_one_way(body_index, polygon_index);
							float one_way_collision_margin = tile_data->get_collision_polygon_one_way_margin(body_index, polygon_index);

							int shapes_count = tile_data->get_collision_polygon_shapes_count(body_index, polygon_index);
							for (int shape_index = 0; shape_index < shapes_count; shape_index++) {
								Transform2D xform = Transform2D();
								xform.set_origin(map_to_world(E_cell->get()) - quadrant_pos);

								// Add decomposed convex shapes.
								Ref<ConvexPolygonShape2D> shape = tile_data->get_collision_polygon_shape(body_index, polygon_index, shape_index);
								ps->body_add_shape(q.bodies[body_index], shape->get_rid(), xform);
								ps->body_set_shape_metadata(q.bodies[body_index], shape_index, E_cell->get());
								ps->body_set_shape_as_one_way_collision(q.bodies[body_index], shape_index, one_way_collision, one_way_collision_margin);
							}
						}
					}
				}
			}
		}

		q_list_element = q_list_element->next();
	}
}

void TileMap::_physics_create_quadrant(TileMapQuadrant *p_quadrant) {
	ERR_FAIL_COND(!tile_set.is_valid());

	//Get the TileMap's gobla transform.
	Transform2D global_transform;
	if (is_inside_tree()) {
		global_transform = get_global_transform();
	}

	// Clear all bodies.
	p_quadrant->bodies.clear();

	// Create the body and set its parameters.
	for (int layer = 0; layer < tile_set->get_physics_layers_count(); layer++) {
		RID body = PhysicsServer2D::get_singleton()->body_create();
		PhysicsServer2D::get_singleton()->body_set_mode(body, PhysicsServer2D::BODY_MODE_STATIC);

		PhysicsServer2D::get_singleton()->body_attach_object_instance_id(body, get_instance_id());
		PhysicsServer2D::get_singleton()->body_set_collision_layer(body, tile_set->get_physics_layer_collision_layer(layer));
		PhysicsServer2D::get_singleton()->body_set_collision_mask(body, tile_set->get_physics_layer_collision_mask(layer));

		Ref<PhysicsMaterial> physics_material = tile_set->get_physics_layer_physics_material(layer);
		if (!physics_material.is_valid()) {
			PhysicsServer2D::get_singleton()->body_set_param(body, PhysicsServer2D::BODY_PARAM_BOUNCE, 0);
			PhysicsServer2D::get_singleton()->body_set_param(body, PhysicsServer2D::BODY_PARAM_FRICTION, 1);
		} else {
			PhysicsServer2D::get_singleton()->body_set_param(body, PhysicsServer2D::BODY_PARAM_BOUNCE, physics_material->computed_bounce());
			PhysicsServer2D::get_singleton()->body_set_param(body, PhysicsServer2D::BODY_PARAM_FRICTION, physics_material->computed_friction());
		}

		if (is_inside_tree()) {
			RID space = get_world_2d()->get_space();
			PhysicsServer2D::get_singleton()->body_set_space(body, space);

			Transform2D xform;
			xform.set_origin(map_to_world(p_quadrant->coords * get_effective_quadrant_size(layer)));
			xform = global_transform * xform;
			PhysicsServer2D::get_singleton()->body_set_state(body, PhysicsServer2D::BODY_STATE_TRANSFORM, xform);
		}

		p_quadrant->bodies.push_back(body);
	}
}

void TileMap::_physics_cleanup_quadrant(TileMapQuadrant *p_quadrant) {
	// Remove a quadrant.
	for (int body_index = 0; body_index < p_quadrant->bodies.size(); body_index++) {
		PhysicsServer2D::get_singleton()->free(p_quadrant->bodies[body_index]);
	}
	p_quadrant->bodies.clear();
}

void TileMap::_physics_draw_quadrant_debug(TileMapQuadrant *p_quadrant) {
	// Draw the debug collision shapes.
	ERR_FAIL_COND(!tile_set.is_valid());

	if (!get_tree()) {
		return;
	}

	bool show_collision = false;
	switch (collision_visibility_mode) {
		case TileMap::VISIBILITY_MODE_DEFAULT:
			show_collision = !Engine::get_singleton()->is_editor_hint() && (get_tree() && get_tree()->is_debugging_navigation_hint());
			break;
		case TileMap::VISIBILITY_MODE_FORCE_HIDE:
			show_collision = false;
			break;
		case TileMap::VISIBILITY_MODE_FORCE_SHOW:
			show_collision = true;
			break;
	}
	if (!show_collision) {
		return;
	}

	RenderingServer *rs = RenderingServer::get_singleton();

	Vector2 quadrant_pos = map_to_world(p_quadrant->coords * get_effective_quadrant_size(p_quadrant->layer));

	Color debug_collision_color = get_tree()->get_debug_collisions_color();
	for (Set<Vector2i>::Element *E_cell = p_quadrant->cells.front(); E_cell; E_cell = E_cell->next()) {
		TileMapCell c = get_cell(p_quadrant->layer, E_cell->get(), true);

		Transform2D xform;
		xform.set_origin(map_to_world(E_cell->get()) - quadrant_pos);
		rs->canvas_item_add_set_transform(p_quadrant->debug_canvas_item, xform);

		if (tile_set->has_source(c.source_id)) {
			TileSetSource *source = *tile_set->get_source(c.source_id);

			if (!source->has_tile(c.get_atlas_coords()) || !source->has_alternative_tile(c.get_atlas_coords(), c.alternative_tile)) {
				continue;
			}

			TileSetAtlasSource *atlas_source = Object::cast_to<TileSetAtlasSource>(source);
			if (atlas_source) {
				TileData *tile_data = Object::cast_to<TileData>(atlas_source->get_tile_data(c.get_atlas_coords(), c.alternative_tile));

				for (int body_index = 0; body_index < p_quadrant->bodies.size(); body_index++) {
					for (int polygon_index = 0; polygon_index < tile_data->get_collision_polygons_count(body_index); polygon_index++) {
						// Draw the debug polygon.
						Vector<Vector2> polygon = tile_data->get_collision_polygon_points(body_index, polygon_index);
						if (polygon.size() >= 3) {
							Vector<Color> color;
							color.push_back(debug_collision_color);
							rs->canvas_item_add_polygon(p_quadrant->debug_canvas_item, polygon, color);
						}
					}
				}
			}
		}
		rs->canvas_item_add_set_transform(p_quadrant->debug_canvas_item, Transform2D());
	}
};

/////////////////////////////// Navigation //////////////////////////////////////

void TileMap::_navigation_notification(int p_what) {
	switch (p_what) {
		case CanvasItem::NOTIFICATION_TRANSFORM_CHANGED: {
			if (is_inside_tree()) {
				for (int layer = 0; layer < (int)layers.size(); layer++) {
					Transform2D tilemap_xform = get_global_transform();
					for (Map<Vector2i, TileMapQuadrant>::Element *E_quadrant = layers[layer].quadrant_map.front(); E_quadrant; E_quadrant = E_quadrant->next()) {
						TileMapQuadrant &q = E_quadrant->get();
						for (Map<Vector2i, Vector<RID>>::Element *E_region = q.navigation_regions.front(); E_region; E_region = E_region->next()) {
							for (int layer_index = 0; layer_index < E_region->get().size(); layer_index++) {
								RID region = E_region->get()[layer_index];
								if (!region.is_valid()) {
									continue;
								}
								Transform2D tile_transform;
								tile_transform.set_origin(map_to_world(E_region->key()));
								NavigationServer2D::get_singleton()->region_set_transform(region, tilemap_xform * tile_transform);
							}
						}
					}
				}
			}
		} break;
	}
}

void TileMap::_navigation_update_dirty_quadrants(SelfList<TileMapQuadrant>::List &r_dirty_quadrant_list) {
	ERR_FAIL_COND(!is_inside_tree());
	ERR_FAIL_COND(!tile_set.is_valid());

	// Get colors for debug.
	SceneTree *st = SceneTree::get_singleton();
	Color debug_navigation_color;
	bool debug_navigation = st && st->is_debugging_navigation_hint();
	if (debug_navigation) {
		debug_navigation_color = st->get_debug_navigation_color();
	}

	Transform2D tilemap_xform = get_global_transform();
	SelfList<TileMapQuadrant> *q_list_element = r_dirty_quadrant_list.first();
	while (q_list_element) {
		TileMapQuadrant &q = *q_list_element->self();

		// Clear navigation shapes in the quadrant.
		for (Map<Vector2i, Vector<RID>>::Element *E = q.navigation_regions.front(); E; E = E->next()) {
			for (int i = 0; i < E->get().size(); i++) {
				RID region = E->get()[i];
				if (!region.is_valid()) {
					continue;
				}
				NavigationServer2D::get_singleton()->region_set_map(region, RID());
			}
		}
		q.navigation_regions.clear();

		// Get the navigation polygons and create regions.
		for (Set<Vector2i>::Element *E_cell = q.cells.front(); E_cell; E_cell = E_cell->next()) {
			TileMapCell c = get_cell(q.layer, E_cell->get(), true);

			TileSetSource *source;
			if (tile_set->has_source(c.source_id)) {
				source = *tile_set->get_source(c.source_id);

				if (!source->has_tile(c.get_atlas_coords()) || !source->has_alternative_tile(c.get_atlas_coords(), c.alternative_tile)) {
					continue;
				}

				TileSetAtlasSource *atlas_source = Object::cast_to<TileSetAtlasSource>(source);
				if (atlas_source) {
					TileData *tile_data = Object::cast_to<TileData>(atlas_source->get_tile_data(c.get_atlas_coords(), c.alternative_tile));
					q.navigation_regions[E_cell->get()].resize(tile_set->get_navigation_layers_count());

					for (int layer_index = 0; layer_index < tile_set->get_navigation_layers_count(); layer_index++) {
						Ref<NavigationPolygon> navpoly;
						navpoly = tile_data->get_navigation_polygon(layer_index);

						if (navpoly.is_valid()) {
							Transform2D tile_transform;
							tile_transform.set_origin(map_to_world(E_cell->get()));

							RID region = NavigationServer2D::get_singleton()->region_create();
							NavigationServer2D::get_singleton()->region_set_map(region, get_world_2d()->get_navigation_map());
							NavigationServer2D::get_singleton()->region_set_transform(region, tilemap_xform * tile_transform);
							NavigationServer2D::get_singleton()->region_set_navpoly(region, navpoly);
							q.navigation_regions[E_cell->get()].write[layer_index] = region;
						}
					}
				}
			}
		}

		q_list_element = q_list_element->next();
	}
}

void TileMap::_navigation_cleanup_quadrant(TileMapQuadrant *p_quadrant) {
	// Clear navigation shapes in the quadrant.
	for (Map<Vector2i, Vector<RID>>::Element *E = p_quadrant->navigation_regions.front(); E; E = E->next()) {
		for (int i = 0; i < E->get().size(); i++) {
			RID region = E->get()[i];
			if (!region.is_valid()) {
				continue;
			}
			NavigationServer2D::get_singleton()->free(region);
		}
	}
	p_quadrant->navigation_regions.clear();
}

void TileMap::_navigation_draw_quadrant_debug(TileMapQuadrant *p_quadrant) {
	// Draw the debug collision shapes.
	ERR_FAIL_COND(!tile_set.is_valid());

	if (!get_tree()) {
		return;
	}

	bool show_navigation = false;
	switch (navigation_visibility_mode) {
		case TileMap::VISIBILITY_MODE_DEFAULT:
			show_navigation = !Engine::get_singleton()->is_editor_hint() && (get_tree() && get_tree()->is_debugging_navigation_hint());
			break;
		case TileMap::VISIBILITY_MODE_FORCE_HIDE:
			show_navigation = false;
			break;
		case TileMap::VISIBILITY_MODE_FORCE_SHOW:
			show_navigation = true;
			break;
	}
	if (!show_navigation) {
		return;
	}

	RenderingServer *rs = RenderingServer::get_singleton();

	Color color = get_tree()->get_debug_navigation_color();
	RandomPCG rand;

	Vector2 quadrant_pos = map_to_world(p_quadrant->coords * get_effective_quadrant_size(p_quadrant->layer));

	for (Set<Vector2i>::Element *E_cell = p_quadrant->cells.front(); E_cell; E_cell = E_cell->next()) {
		TileMapCell c = get_cell(p_quadrant->layer, E_cell->get(), true);

		TileSetSource *source;
		if (tile_set->has_source(c.source_id)) {
			source = *tile_set->get_source(c.source_id);

			if (!source->has_tile(c.get_atlas_coords()) || !source->has_alternative_tile(c.get_atlas_coords(), c.alternative_tile)) {
				continue;
			}

			TileSetAtlasSource *atlas_source = Object::cast_to<TileSetAtlasSource>(source);
			if (atlas_source) {
				TileData *tile_data = Object::cast_to<TileData>(atlas_source->get_tile_data(c.get_atlas_coords(), c.alternative_tile));

				Transform2D xform;
				xform.set_origin(map_to_world(E_cell->get()) - quadrant_pos);
				rs->canvas_item_add_set_transform(p_quadrant->debug_canvas_item, xform);

				for (int layer_index = 0; layer_index < tile_set->get_navigation_layers_count(); layer_index++) {
					Ref<NavigationPolygon> navpoly = tile_data->get_navigation_polygon(layer_index);
					if (navpoly.is_valid()) {
						PackedVector2Array navigation_polygon_vertices = navpoly->get_vertices();

						for (int i = 0; i < navpoly->get_polygon_count(); i++) {
							// An array of vertices for this polygon.
							Vector<int> polygon = navpoly->get_polygon(i);
							Vector<Vector2> vertices;
							vertices.resize(polygon.size());
							for (int j = 0; j < polygon.size(); j++) {
								ERR_FAIL_INDEX(polygon[j], navigation_polygon_vertices.size());
								vertices.write[j] = navigation_polygon_vertices[polygon[j]];
							}

							// Generate the polygon color, slightly randomly modified from the settings one.
							Color random_variation_color;
							random_variation_color.set_hsv(color.get_h() + rand.random(-1.0, 1.0) * 0.05, color.get_s(), color.get_v() + rand.random(-1.0, 1.0) * 0.1);
							random_variation_color.a = color.a;
							Vector<Color> colors;
							colors.push_back(random_variation_color);

							rs->canvas_item_add_polygon(p_quadrant->debug_canvas_item, vertices, colors);
						}
					}
				}
			}
		}
	}
}

/////////////////////////////// Scenes //////////////////////////////////////

void TileMap::_scenes_update_dirty_quadrants(SelfList<TileMapQuadrant>::List &r_dirty_quadrant_list) {
	ERR_FAIL_COND(!tile_set.is_valid());

	SelfList<TileMapQuadrant> *q_list_element = r_dirty_quadrant_list.first();
	while (q_list_element) {
		TileMapQuadrant &q = *q_list_element->self();

		// Clear the scenes.
		for (Map<Vector2i, String>::Element *E = q.scenes.front(); E; E = E->next()) {
			Node *node = get_node(E->get());
			if (node) {
				node->queue_delete();
			}
		}

		q.scenes.clear();

		// Recreate the scenes.
		for (Set<Vector2i>::Element *E_cell = q.cells.front(); E_cell; E_cell = E_cell->next()) {
			const TileMapCell &c = get_cell(q.layer, E_cell->get(), true);

			TileSetSource *source;
			if (tile_set->has_source(c.source_id)) {
				source = *tile_set->get_source(c.source_id);

				if (!source->has_tile(c.get_atlas_coords()) || !source->has_alternative_tile(c.get_atlas_coords(), c.alternative_tile)) {
					continue;
				}

				TileSetScenesCollectionSource *scenes_collection_source = Object::cast_to<TileSetScenesCollectionSource>(source);
				if (scenes_collection_source) {
					Ref<PackedScene> packed_scene = scenes_collection_source->get_scene_tile_scene(c.alternative_tile);
					if (packed_scene.is_valid()) {
						Node *scene = packed_scene->instantiate();
						add_child(scene);
						Control *scene_as_control = Object::cast_to<Control>(scene);
						Node2D *scene_as_node2d = Object::cast_to<Node2D>(scene);
						if (scene_as_control) {
							scene_as_control->set_position(map_to_world(E_cell->get()) + scene_as_control->get_position());
						} else if (scene_as_node2d) {
							Transform2D xform;
							xform.set_origin(map_to_world(E_cell->get()));
							scene_as_node2d->set_transform(xform * scene_as_node2d->get_transform());
						}
						q.scenes[E_cell->get()] = scene->get_name();
					}
				}
			}
		}

		q_list_element = q_list_element->next();
	}
}

void TileMap::_scenes_cleanup_quadrant(TileMapQuadrant *p_quadrant) {
	// Clear the scenes.
	for (Map<Vector2i, String>::Element *E = p_quadrant->scenes.front(); E; E = E->next()) {
		Node *node = get_node(E->get());
		if (node) {
			node->queue_delete();
		}
	}

	p_quadrant->scenes.clear();
}

void TileMap::_scenes_draw_quadrant_debug(TileMapQuadrant *p_quadrant) {
	ERR_FAIL_COND(!tile_set.is_valid());

	if (!Engine::get_singleton()->is_editor_hint()) {
		return;
	}

	// Draw a placeholder for scenes needing one.
	RenderingServer *rs = RenderingServer::get_singleton();
	Vector2 quadrant_pos = map_to_world(p_quadrant->coords * get_effective_quadrant_size(p_quadrant->layer));
	for (Set<Vector2i>::Element *E_cell = p_quadrant->cells.front(); E_cell; E_cell = E_cell->next()) {
		const TileMapCell &c = get_cell(p_quadrant->layer, E_cell->get(), true);

		TileSetSource *source;
		if (tile_set->has_source(c.source_id)) {
			source = *tile_set->get_source(c.source_id);

			if (!source->has_tile(c.get_atlas_coords()) || !source->has_alternative_tile(c.get_atlas_coords(), c.alternative_tile)) {
				continue;
			}

			TileSetScenesCollectionSource *scenes_collection_source = Object::cast_to<TileSetScenesCollectionSource>(source);
			if (scenes_collection_source) {
				if (!scenes_collection_source->get_scene_tile_scene(c.alternative_tile).is_valid() || scenes_collection_source->get_scene_tile_display_placeholder(c.alternative_tile)) {
					// Generate a random color from the hashed values of the tiles.
					Array to_hash;
					to_hash.push_back(c.source_id);
					to_hash.push_back(c.alternative_tile);
					uint32_t hash = RandomPCG(to_hash.hash()).rand();

					Color color;
					color = color.from_hsv(
							(float)((hash >> 24) & 0xFF) / 256.0,
							Math::lerp(0.5, 1.0, (float)((hash >> 16) & 0xFF) / 256.0),
							Math::lerp(0.5, 1.0, (float)((hash >> 8) & 0xFF) / 256.0),
							0.8);

					// Draw a placeholder tile.
					Transform2D xform;
					xform.set_origin(map_to_world(E_cell->get()) - quadrant_pos);
					rs->canvas_item_add_set_transform(p_quadrant->debug_canvas_item, xform);
					rs->canvas_item_add_circle(p_quadrant->debug_canvas_item, Vector2(), MIN(tile_set->get_tile_size().x, tile_set->get_tile_size().y) / 4.0, color);
				}
			}
		}
	}
}

void TileMap::set_cell(int p_layer, const Vector2i &p_coords, int p_source_id, const Vector2i p_atlas_coords, int p_alternative_tile) {
	ERR_FAIL_INDEX(p_layer, (int)layers.size());

	// Set the current cell tile (using integer position).
	Map<Vector2i, TileMapCell> &tile_map = layers[p_layer].tile_map;
	Vector2i pk(p_coords);
	Map<Vector2i, TileMapCell>::Element *E = tile_map.find(pk);

	int source_id = p_source_id;
	Vector2i atlas_coords = p_atlas_coords;
	int alternative_tile = p_alternative_tile;

	if ((source_id == TileSet::INVALID_SOURCE || atlas_coords == TileSetSource::INVALID_ATLAS_COORDS || alternative_tile == TileSetSource::INVALID_TILE_ALTERNATIVE) &&
			(source_id != TileSet::INVALID_SOURCE || atlas_coords != TileSetSource::INVALID_ATLAS_COORDS || alternative_tile != TileSetSource::INVALID_TILE_ALTERNATIVE)) {
		WARN_PRINT("Setting a cell a cell as empty requires both source_id, atlas_coord and alternative_tile to be set to their respective \"invalid\" values. Values were thus changes accordingly.");
		source_id = TileSet::INVALID_SOURCE;
		atlas_coords = TileSetSource::INVALID_ATLAS_COORDS;
		alternative_tile = TileSetSource::INVALID_TILE_ALTERNATIVE;
	}

	if (!E && source_id == TileSet::INVALID_SOURCE) {
		return; // Nothing to do, the tile is already empty.
	}

	// Get the quadrant
	Vector2i qk = _coords_to_quadrant_coords(p_layer, pk);

	Map<Vector2i, TileMapQuadrant>::Element *Q = layers[p_layer].quadrant_map.find(qk);

	if (source_id == TileSet::INVALID_SOURCE) {
		// Erase existing cell in the tile map.
		tile_map.erase(pk);

		// Erase existing cell in the quadrant.
		ERR_FAIL_COND(!Q);
		TileMapQuadrant &q = Q->get();

		q.cells.erase(pk);

		// Remove or make the quadrant dirty.
		if (q.cells.size() == 0) {
			_erase_quadrant(Q);
		} else {
			_make_quadrant_dirty(Q);
		}

		used_rect_cache_dirty = true;
	} else {
		if (!E) {
			// Insert a new cell in the tile map.
			E = tile_map.insert(pk, TileMapCell());

			// Create a new quadrant if needed, then insert the cell if needed.
			if (!Q) {
				Q = _create_quadrant(p_layer, qk);
			}
			TileMapQuadrant &q = Q->get();
			q.cells.insert(pk);

		} else {
			ERR_FAIL_COND(!Q); // TileMapQuadrant should exist...

			if (E->get().source_id == source_id && E->get().get_atlas_coords() == atlas_coords && E->get().alternative_tile == alternative_tile) {
				return; // Nothing changed.
			}
		}

		TileMapCell &c = E->get();

		c.source_id = source_id;
		c.set_atlas_coords(atlas_coords);
		c.alternative_tile = alternative_tile;

		_make_quadrant_dirty(Q);
		used_rect_cache_dirty = true;
	}
}

int TileMap::get_cell_source_id(int p_layer, const Vector2i &p_coords, bool p_use_proxies) const {
	ERR_FAIL_INDEX_V(p_layer, (int)layers.size(), TileSet::INVALID_SOURCE);

	// Get a cell source id from position
	const Map<Vector2i, TileMapCell> &tile_map = layers[p_layer].tile_map;
	const Map<Vector2i, TileMapCell>::Element *E = tile_map.find(p_coords);

	if (!E) {
		return TileSet::INVALID_SOURCE;
	}

	if (p_use_proxies && tile_set.is_valid()) {
		Array proxyed = tile_set->map_tile_proxy(E->get().source_id, E->get().get_atlas_coords(), E->get().alternative_tile);
		return proxyed[0];
	}

	return E->get().source_id;
}

Vector2i TileMap::get_cell_atlas_coords(int p_layer, const Vector2i &p_coords, bool p_use_proxies) const {
	ERR_FAIL_INDEX_V(p_layer, (int)layers.size(), TileSetSource::INVALID_ATLAS_COORDS);

	// Get a cell source id from position
	const Map<Vector2i, TileMapCell> &tile_map = layers[p_layer].tile_map;
	const Map<Vector2i, TileMapCell>::Element *E = tile_map.find(p_coords);

	if (!E) {
		return TileSetSource::INVALID_ATLAS_COORDS;
	}

	if (p_use_proxies && tile_set.is_valid()) {
		Array proxyed = tile_set->map_tile_proxy(E->get().source_id, E->get().get_atlas_coords(), E->get().alternative_tile);
		return proxyed[1];
	}

	return E->get().get_atlas_coords();
}

int TileMap::get_cell_alternative_tile(int p_layer, const Vector2i &p_coords, bool p_use_proxies) const {
	ERR_FAIL_INDEX_V(p_layer, (int)layers.size(), TileSetSource::INVALID_TILE_ALTERNATIVE);

	// Get a cell source id from position
	const Map<Vector2i, TileMapCell> &tile_map = layers[p_layer].tile_map;
	const Map<Vector2i, TileMapCell>::Element *E = tile_map.find(p_coords);

	if (!E) {
		return TileSetSource::INVALID_TILE_ALTERNATIVE;
	}

	if (p_use_proxies && tile_set.is_valid()) {
		Array proxyed = tile_set->map_tile_proxy(E->get().source_id, E->get().get_atlas_coords(), E->get().alternative_tile);
		return proxyed[2];
	}

	return E->get().alternative_tile;
}

TileMapPattern *TileMap::get_pattern(int p_layer, TypedArray<Vector2i> p_coords_array) {
	ERR_FAIL_INDEX_V(p_layer, (int)layers.size(), nullptr);
	ERR_FAIL_COND_V(!tile_set.is_valid(), nullptr);

	TileMapPattern *output = memnew(TileMapPattern);
	if (p_coords_array.is_empty()) {
		return output;
	}

	Vector2i min = Vector2i(p_coords_array[0]);
	for (int i = 1; i < p_coords_array.size(); i++) {
		min = min.min(p_coords_array[i]);
	}

	Vector<Vector2i> coords_in_pattern_array;
	coords_in_pattern_array.resize(p_coords_array.size());
	Vector2i ensure_positive_offset;
	for (int i = 0; i < p_coords_array.size(); i++) {
		Vector2i coords = p_coords_array[i];
		Vector2i coords_in_pattern = coords - min;
		if (tile_set->get_tile_shape() != TileSet::TILE_SHAPE_SQUARE) {
			if (tile_set->get_tile_layout() == TileSet::TILE_LAYOUT_STACKED) {
				if (tile_set->get_tile_offset_axis() == TileSet::TILE_OFFSET_AXIS_HORIZONTAL && bool(min.y % 2) && bool(coords_in_pattern.y % 2)) {
					coords_in_pattern.x -= 1;
					if (coords_in_pattern.x < 0) {
						ensure_positive_offset.x = 1;
					}
				} else if (tile_set->get_tile_offset_axis() == TileSet::TILE_OFFSET_AXIS_VERTICAL && bool(min.x % 2) && bool(coords_in_pattern.x % 2)) {
					coords_in_pattern.y -= 1;
					if (coords_in_pattern.y < 0) {
						ensure_positive_offset.y = 1;
					}
				}
			} else if (tile_set->get_tile_layout() == TileSet::TILE_LAYOUT_STACKED_OFFSET) {
				if (tile_set->get_tile_offset_axis() == TileSet::TILE_OFFSET_AXIS_HORIZONTAL && bool(min.y % 2) && bool(coords_in_pattern.y % 2)) {
					coords_in_pattern.x += 1;
				} else if (tile_set->get_tile_offset_axis() == TileSet::TILE_OFFSET_AXIS_VERTICAL && bool(min.x % 2) && bool(coords_in_pattern.x % 2)) {
					coords_in_pattern.y += 1;
				}
			}
		}
		coords_in_pattern_array.write[i] = coords_in_pattern;
	}

	for (int i = 0; i < coords_in_pattern_array.size(); i++) {
		Vector2i coords = p_coords_array[i];
		Vector2i coords_in_pattern = coords_in_pattern_array[i];
		output->set_cell(coords_in_pattern + ensure_positive_offset, get_cell_source_id(p_layer, coords), get_cell_atlas_coords(p_layer, coords), get_cell_alternative_tile(p_layer, coords));
	}

	return output;
}

Vector2i TileMap::map_pattern(Vector2i p_position_in_tilemap, Vector2i p_coords_in_pattern, const TileMapPattern *p_pattern) {
	ERR_FAIL_COND_V(!p_pattern->has_cell(p_coords_in_pattern), Vector2i());

	Vector2i output = p_position_in_tilemap + p_coords_in_pattern;
	if (tile_set->get_tile_shape() != TileSet::TILE_SHAPE_SQUARE) {
		if (tile_set->get_tile_layout() == TileSet::TILE_LAYOUT_STACKED) {
			if (tile_set->get_tile_offset_axis() == TileSet::TILE_OFFSET_AXIS_HORIZONTAL && bool(p_position_in_tilemap.y % 2) && bool(p_coords_in_pattern.y % 2)) {
				output.x += 1;
			} else if (tile_set->get_tile_offset_axis() == TileSet::TILE_OFFSET_AXIS_VERTICAL && bool(p_position_in_tilemap.x % 2) && bool(p_coords_in_pattern.x % 2)) {
				output.y += 1;
			}
		} else if (tile_set->get_tile_layout() == TileSet::TILE_LAYOUT_STACKED_OFFSET) {
			if (tile_set->get_tile_offset_axis() == TileSet::TILE_OFFSET_AXIS_HORIZONTAL && bool(p_position_in_tilemap.y % 2) && bool(p_coords_in_pattern.y % 2)) {
				output.x -= 1;
			} else if (tile_set->get_tile_offset_axis() == TileSet::TILE_OFFSET_AXIS_VERTICAL && bool(p_position_in_tilemap.x % 2) && bool(p_coords_in_pattern.x % 2)) {
				output.y -= 1;
			}
		}
	}

	return output;
}

void TileMap::set_pattern(int p_layer, Vector2i p_position, const TileMapPattern *p_pattern) {
	ERR_FAIL_INDEX(p_layer, (int)layers.size());
	ERR_FAIL_COND(!tile_set.is_valid());

	TypedArray<Vector2i> used_cells = p_pattern->get_used_cells();
	for (int i = 0; i < used_cells.size(); i++) {
		Vector2i coords = map_pattern(p_position, used_cells[i], p_pattern);
		set_cell(p_layer, coords, p_pattern->get_cell_source_id(coords), p_pattern->get_cell_atlas_coords(coords), p_pattern->get_cell_alternative_tile(coords));
	}
}

TileMapCell TileMap::get_cell(int p_layer, const Vector2i &p_coords, bool p_use_proxies) const {
	ERR_FAIL_INDEX_V(p_layer, (int)layers.size(), TileMapCell());
	const Map<Vector2i, TileMapCell> &tile_map = layers[p_layer].tile_map;
	if (!tile_map.has(p_coords)) {
		return TileMapCell();
	} else {
		TileMapCell c = tile_map.find(p_coords)->get();
		if (p_use_proxies && tile_set.is_valid()) {
			Array proxyed = tile_set->map_tile_proxy(c.source_id, c.get_atlas_coords(), c.alternative_tile);
			c.source_id = proxyed[0];
			c.set_atlas_coords(proxyed[1]);
			c.alternative_tile = proxyed[2];
		}
		return c;
	}
}

Map<Vector2i, TileMapQuadrant> *TileMap::get_quadrant_map(int p_layer) {
	ERR_FAIL_INDEX_V(p_layer, (int)layers.size(), nullptr);

	return &layers[p_layer].quadrant_map;
}

void TileMap::fix_invalid_tiles() {
	ERR_FAIL_COND_MSG(tile_set.is_null(), "Cannot fix invalid tiles if Tileset is not open.");

	for (unsigned int i = 0; i < layers.size(); i++) {
		const Map<Vector2i, TileMapCell> &tile_map = layers[i].tile_map;
		Set<Vector2i> coords;
		for (Map<Vector2i, TileMapCell>::Element *E = tile_map.front(); E; E = E->next()) {
			TileSetSource *source = *tile_set->get_source(E->get().source_id);
			if (!source || !source->has_tile(E->get().get_atlas_coords()) || !source->has_alternative_tile(E->get().get_atlas_coords(), E->get().alternative_tile)) {
				coords.insert(E->key());
			}
		}
		for (Set<Vector2i>::Element *E = coords.front(); E; E = E->next()) {
			set_cell(i, E->get(), TileSet::INVALID_SOURCE, TileSetSource::INVALID_ATLAS_COORDS, TileSetSource::INVALID_TILE_ALTERNATIVE);
		}
	}
}

void TileMap::clear_layer(int p_layer) {
	ERR_FAIL_INDEX(p_layer, (int)layers.size());

	// Remove all tiles.
	_clear_layer_internals(p_layer);
	layers[p_layer].tile_map.clear();

	used_rect_cache_dirty = true;
}

void TileMap::clear() {
	// Remove all tiles.
	_clear_internals();
	for (unsigned int i = 0; i < layers.size(); i++) {
		layers[i].tile_map.clear();
	}
	used_rect_cache_dirty = true;
}

void TileMap::_set_tile_data(int p_layer, const Vector<int> &p_data) {
	ERR_FAIL_INDEX(p_layer, (int)layers.size());
	ERR_FAIL_COND(format > FORMAT_3);

	// Set data for a given tile from raw data.

	int c = p_data.size();
	const int *r = p_data.ptr();

	int offset = (format >= FORMAT_2) ? 3 : 2;
	ERR_FAIL_COND_MSG(c % offset != 0, "Corrupted tile data.");

	clear_layer(p_layer);

#ifdef DISABLE_DEPRECATED
	ERR_FAIL_COND_MSG(format != FORMAT_3, vformat("Cannot handle deprecated TileMap data format version %d. This Godot version was compiled with no support for deprecated data.", format));
#endif

	for (int i = 0; i < c; i += offset) {
		const uint8_t *ptr = (const uint8_t *)&r[i];
		uint8_t local[12];
		for (int j = 0; j < ((format >= FORMAT_2) ? 12 : 8); j++) {
			local[j] = ptr[j];
		}

#ifdef BIG_ENDIAN_ENABLED

		SWAP(local[0], local[3]);
		SWAP(local[1], local[2]);
		SWAP(local[4], local[7]);
		SWAP(local[5], local[6]);
		//TODO: ask someone to check this...
		if (FORMAT >= FORMAT_2) {
			SWAP(local[8], local[11]);
			SWAP(local[9], local[10]);
		}
#endif
		// Extracts position in TileMap.
		int16_t x = decode_uint16(&local[0]);
		int16_t y = decode_uint16(&local[2]);

		if (format == FORMAT_3) {
			uint16_t source_id = decode_uint16(&local[4]);
			uint16_t atlas_coords_x = decode_uint16(&local[6]);
			uint16_t atlas_coords_y = decode_uint16(&local[8]);
			uint16_t alternative_tile = decode_uint16(&local[10]);
			set_cell(p_layer, Vector2i(x, y), source_id, Vector2i(atlas_coords_x, atlas_coords_y), alternative_tile);
		} else {
#ifndef DISABLE_DEPRECATED
			// Previous decated format.

			uint32_t v = decode_uint32(&local[4]);
			// Extract the transform flags that used to be in the tilemap.
			bool flip_h = v & (1 << 29);
			bool flip_v = v & (1 << 30);
			bool transpose = v & (1 << 31);
			v &= (1 << 29) - 1;

			// Extract autotile/atlas coords.
			int16_t coord_x = 0;
			int16_t coord_y = 0;
			if (format == FORMAT_2) {
				coord_x = decode_uint16(&local[8]);
				coord_y = decode_uint16(&local[10]);
			}

			if (tile_set.is_valid()) {
				Array a = tile_set->compatibility_tilemap_map(v, Vector2i(coord_x, coord_y), flip_h, flip_v, transpose);
				if (a.size() == 3) {
					set_cell(p_layer, Vector2i(x, y), a[0], a[1], a[2]);
				} else {
					ERR_PRINT(vformat("No valid tile in Tileset for: tile:%s coords:%s flip_h:%s flip_v:%s transpose:%s", v, Vector2i(coord_x, coord_y), flip_h, flip_v, transpose));
				}
			} else {
				int compatibility_alternative_tile = ((int)flip_h) + ((int)flip_v << 1) + ((int)transpose << 2);
				set_cell(p_layer, Vector2i(x, y), v, Vector2i(coord_x, coord_y), compatibility_alternative_tile);
			}
#endif
		}
	}
	emit_signal(SNAME("changed"));
}

Vector<int> TileMap::_get_tile_data(int p_layer) const {
	ERR_FAIL_INDEX_V(p_layer, (int)layers.size(), Vector<int>());

	// Export tile data to raw format
	const Map<Vector2i, TileMapCell> &tile_map = layers[p_layer].tile_map;
	Vector<int> data;
	data.resize(tile_map.size() * 3);
	int *w = data.ptrw();

	// Save in highest format

	int idx = 0;
	for (const Map<Vector2i, TileMapCell>::Element *E = tile_map.front(); E; E = E->next()) {
		uint8_t *ptr = (uint8_t *)&w[idx];
		encode_uint16((int16_t)(E->key().x), &ptr[0]);
		encode_uint16((int16_t)(E->key().y), &ptr[2]);
		encode_uint16(E->get().source_id, &ptr[4]);
		encode_uint16(E->get().coord_x, &ptr[6]);
		encode_uint16(E->get().coord_y, &ptr[8]);
		encode_uint16(E->get().alternative_tile, &ptr[10]);
		idx += 3;
	}

	return data;
}

#ifdef TOOLS_ENABLED
Rect2 TileMap::_edit_get_rect() const {
	// Return the visible rect of the tilemap
	if (pending_update) {
		const_cast<TileMap *>(this)->_update_dirty_quadrants();
	} else {
		const_cast<TileMap *>(this)->_recompute_rect_cache();
	}
	return rect_cache;
}
#endif

bool TileMap::_set(const StringName &p_name, const Variant &p_value) {
	Vector<String> components = String(p_name).split("/", true, 2);
	if (p_name == "format") {
		if (p_value.get_type() == Variant::INT) {
			format = (DataFormat)(p_value.operator int64_t()); // Set format used for loading
			return true;
		}
	} else if (p_name == "tile_data") { // Kept for compatibility reasons.
		if (p_value.is_array()) {
			if (layers.size() < 1) {
				layers.resize(1);
			}
			_set_tile_data(0, p_value);
			return true;
		}
		return false;
	} else if (components.size() == 2 && components[0].begins_with("layer_") && components[0].trim_prefix("layer_").is_valid_int()) {
		int index = components[0].trim_prefix("layer_").to_int();
		if (index < 0 || index >= (int)layers.size()) {
			return false;
		}

		if (components[1] == "name") {
			set_layer_name(index, p_value);
			return true;
		} else if (components[1] == "enabled") {
			set_layer_enabled(index, p_value);
			return true;
		} else if (components[1] == "y_sort_enabled") {
			set_layer_y_sort_enabled(index, p_value);
			return true;
		} else if (components[1] == "y_sort_origin") {
			set_layer_y_sort_origin(index, p_value);
			return true;
		} else if (components[1] == "z_index") {
			set_layer_z_index(index, p_value);
			return true;
		} else if (components[1] == "tile_data") {
			_set_tile_data(index, p_value);
			return true;
		} else {
			return false;
		}
	}
	return false;
}

bool TileMap::_get(const StringName &p_name, Variant &r_ret) const {
	Vector<String> components = String(p_name).split("/", true, 2);
	if (p_name == "format") {
		r_ret = FORMAT_3; // When saving, always save highest format
		return true;
	} else if (components.size() == 2 && components[0].begins_with("layer_") && components[0].trim_prefix("layer_").is_valid_int()) {
		int index = components[0].trim_prefix("layer_").to_int();
		if (index < 0 || index >= (int)layers.size()) {
			return false;
		}

		if (components[1] == "name") {
			r_ret = get_layer_name(index);
			return true;
		} else if (components[1] == "enabled") {
			r_ret = is_layer_enabled(index);
			return true;
		} else if (components[1] == "y_sort_enabled") {
			r_ret = is_layer_y_sort_enabled(index);
			return true;
		} else if (components[1] == "y_sort_origin") {
			r_ret = get_layer_y_sort_origin(index);
			return true;
		} else if (components[1] == "z_index") {
			r_ret = get_layer_z_index(index);
			return true;
		} else if (components[1] == "tile_data") {
			r_ret = _get_tile_data(index);
			return true;
		} else {
			return false;
		}
	}
	return false;
}

void TileMap::_get_property_list(List<PropertyInfo> *p_list) const {
	p_list->push_back(PropertyInfo(Variant::INT, "format", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NOEDITOR | PROPERTY_USAGE_INTERNAL));
	p_list->push_back(PropertyInfo(Variant::NIL, "Layers", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_GROUP));
	for (unsigned int i = 0; i < layers.size(); i++) {
		p_list->push_back(PropertyInfo(Variant::STRING, vformat("layer_%d/name", i), PROPERTY_HINT_NONE));
		p_list->push_back(PropertyInfo(Variant::BOOL, vformat("layer_%d/enabled", i), PROPERTY_HINT_NONE));
		p_list->push_back(PropertyInfo(Variant::BOOL, vformat("layer_%d/y_sort_enabled", i), PROPERTY_HINT_NONE));
		p_list->push_back(PropertyInfo(Variant::INT, vformat("layer_%d/y_sort_origin", i), PROPERTY_HINT_NONE));
		p_list->push_back(PropertyInfo(Variant::INT, vformat("layer_%d/z_index", i), PROPERTY_HINT_NONE));
		p_list->push_back(PropertyInfo(Variant::OBJECT, vformat("layer_%d/tile_data", i), PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NOEDITOR));
	}
}

Vector2 TileMap::map_to_world(const Vector2i &p_pos) const {
	// SHOULD RETURN THE CENTER OF THE TILE
	ERR_FAIL_COND_V(!tile_set.is_valid(), Vector2());

	Vector2 ret = p_pos;
	TileSet::TileShape tile_shape = tile_set->get_tile_shape();
	TileSet::TileOffsetAxis tile_offset_axis = tile_set->get_tile_offset_axis();

	if (tile_shape == TileSet::TILE_SHAPE_HALF_OFFSET_SQUARE || tile_shape == TileSet::TILE_SHAPE_HEXAGON || tile_shape == TileSet::TILE_SHAPE_ISOMETRIC) {
		// Technically, those 3 shapes are equivalent, as they are basically half-offset, but with different levels or overlap.
		// square = no overlap, hexagon = 0.25 overlap, isometric = 0.5 overlap
		if (tile_offset_axis == TileSet::TILE_OFFSET_AXIS_HORIZONTAL) {
			switch (tile_set->get_tile_layout()) {
				case TileSet::TILE_LAYOUT_STACKED:
					ret = Vector2(ret.x + (Math::posmod(ret.y, 2) == 0 ? 0.0 : 0.5), ret.y);
					break;
				case TileSet::TILE_LAYOUT_STACKED_OFFSET:
					ret = Vector2(ret.x + (Math::posmod(ret.y, 2) == 1 ? 0.0 : 0.5), ret.y);
					break;
				case TileSet::TILE_LAYOUT_STAIRS_RIGHT:
					ret = Vector2(ret.x + ret.y / 2, ret.y);
					break;
				case TileSet::TILE_LAYOUT_STAIRS_DOWN:
					ret = Vector2(ret.x / 2, ret.y * 2 + ret.x);
					break;
				case TileSet::TILE_LAYOUT_DIAMOND_RIGHT:
					ret = Vector2((ret.x + ret.y) / 2, ret.y - ret.x);
					break;
				case TileSet::TILE_LAYOUT_DIAMOND_DOWN:
					ret = Vector2((ret.x - ret.y) / 2, ret.y + ret.x);
					break;
			}
		} else { // TILE_OFFSET_AXIS_VERTICAL
			switch (tile_set->get_tile_layout()) {
				case TileSet::TILE_LAYOUT_STACKED:
					ret = Vector2(ret.x, ret.y + (Math::posmod(ret.x, 2) == 0 ? 0.0 : 0.5));
					break;
				case TileSet::TILE_LAYOUT_STACKED_OFFSET:
					ret = Vector2(ret.x, ret.y + (Math::posmod(ret.x, 2) == 1 ? 0.0 : 0.5));
					break;
				case TileSet::TILE_LAYOUT_STAIRS_RIGHT:
					ret = Vector2(ret.x * 2 + ret.y, ret.y / 2);
					break;
				case TileSet::TILE_LAYOUT_STAIRS_DOWN:
					ret = Vector2(ret.x, ret.y + ret.x / 2);
					break;
				case TileSet::TILE_LAYOUT_DIAMOND_RIGHT:
					ret = Vector2(ret.x + ret.y, (ret.y - ret.x) / 2);
					break;
				case TileSet::TILE_LAYOUT_DIAMOND_DOWN:
					ret = Vector2(ret.x - ret.y, (ret.y + ret.x) / 2);
					break;
			}
		}
	}

	// Multiply by the overlapping ratio
	double overlapping_ratio = 1.0;
	if (tile_offset_axis == TileSet::TILE_OFFSET_AXIS_HORIZONTAL) {
		if (tile_shape == TileSet::TILE_SHAPE_ISOMETRIC) {
			overlapping_ratio = 0.5;
		} else if (tile_shape == TileSet::TILE_SHAPE_HEXAGON) {
			overlapping_ratio = 0.75;
		}
		ret.y *= overlapping_ratio;
	} else { // TILE_OFFSET_AXIS_VERTICAL
		if (tile_shape == TileSet::TILE_SHAPE_ISOMETRIC) {
			overlapping_ratio = 0.5;
		} else if (tile_shape == TileSet::TILE_SHAPE_HEXAGON) {
			overlapping_ratio = 0.75;
		}
		ret.x *= overlapping_ratio;
	}

	return (ret + Vector2(0.5, 0.5)) * tile_set->get_tile_size();
}

Vector2i TileMap::world_to_map(const Vector2 &p_pos) const {
	ERR_FAIL_COND_V(!tile_set.is_valid(), Vector2i());

	Vector2 ret = p_pos;
	ret /= tile_set->get_tile_size();

	TileSet::TileShape tile_shape = tile_set->get_tile_shape();
	TileSet::TileOffsetAxis tile_offset_axis = tile_set->get_tile_offset_axis();
	TileSet::TileLayout tile_layout = tile_set->get_tile_layout();

	// Divide by the overlapping ratio
	double overlapping_ratio = 1.0;
	if (tile_offset_axis == TileSet::TILE_OFFSET_AXIS_HORIZONTAL) {
		if (tile_shape == TileSet::TILE_SHAPE_ISOMETRIC) {
			overlapping_ratio = 0.5;
		} else if (tile_shape == TileSet::TILE_SHAPE_HEXAGON) {
			overlapping_ratio = 0.75;
		}
		ret.y /= overlapping_ratio;
	} else { // TILE_OFFSET_AXIS_VERTICAL
		if (tile_shape == TileSet::TILE_SHAPE_ISOMETRIC) {
			overlapping_ratio = 0.5;
		} else if (tile_shape == TileSet::TILE_SHAPE_HEXAGON) {
			overlapping_ratio = 0.75;
		}
		ret.x /= overlapping_ratio;
	}

	// For each half-offset shape, we check if we are in the corner of the tile, and thus should correct the world position accordingly.
	if (tile_shape == TileSet::TILE_SHAPE_HALF_OFFSET_SQUARE || tile_shape == TileSet::TILE_SHAPE_HEXAGON || tile_shape == TileSet::TILE_SHAPE_ISOMETRIC) {
		// Technically, those 3 shapes are equivalent, as they are basically half-offset, but with different levels or overlap.
		// square = no overlap, hexagon = 0.25 overlap, isometric = 0.5 overlap
		if (tile_offset_axis == TileSet::TILE_OFFSET_AXIS_HORIZONTAL) {
			// Smart floor of the position
			Vector2 raw_pos = ret;
			if (Math::posmod(Math::floor(ret.y), 2) ^ (tile_layout == TileSet::TILE_LAYOUT_STACKED_OFFSET)) {
				ret = Vector2(Math::floor(ret.x + 0.5) - 0.5, Math::floor(ret.y));
			} else {
				ret = ret.floor();
			}

			// Compute the tile offset, and if we might the output for a neighbour top tile
			Vector2 in_tile_pos = raw_pos - ret;
			bool in_top_left_triangle = (in_tile_pos - Vector2(0.5, 0.0)).cross(Vector2(-0.5, 1.0 / overlapping_ratio - 1)) <= 0;
			bool in_top_right_triangle = (in_tile_pos - Vector2(0.5, 0.0)).cross(Vector2(0.5, 1.0 / overlapping_ratio - 1)) > 0;

			switch (tile_layout) {
				case TileSet::TILE_LAYOUT_STACKED:
					ret = ret.floor();
					if (in_top_left_triangle) {
						ret += Vector2i(Math::posmod(Math::floor(ret.y), 2) ? 0 : -1, -1);
					} else if (in_top_right_triangle) {
						ret += Vector2i(Math::posmod(Math::floor(ret.y), 2) ? 1 : 0, -1);
					}
					break;
				case TileSet::TILE_LAYOUT_STACKED_OFFSET:
					ret = ret.floor();
					if (in_top_left_triangle) {
						ret += Vector2i(Math::posmod(Math::floor(ret.y), 2) ? -1 : 0, -1);
					} else if (in_top_right_triangle) {
						ret += Vector2i(Math::posmod(Math::floor(ret.y), 2) ? 0 : 1, -1);
					}
					break;
				case TileSet::TILE_LAYOUT_STAIRS_RIGHT:
					ret = Vector2(ret.x - ret.y / 2, ret.y).floor();
					if (in_top_left_triangle) {
						ret += Vector2i(0, -1);
					} else if (in_top_right_triangle) {
						ret += Vector2i(1, -1);
					}
					break;
				case TileSet::TILE_LAYOUT_STAIRS_DOWN:
					ret = Vector2(ret.x * 2, ret.y / 2 - ret.x).floor();
					if (in_top_left_triangle) {
						ret += Vector2i(-1, 0);
					} else if (in_top_right_triangle) {
						ret += Vector2i(1, -1);
					}
					break;
				case TileSet::TILE_LAYOUT_DIAMOND_RIGHT:
					ret = Vector2(ret.x - ret.y / 2, ret.y / 2 + ret.x).floor();
					if (in_top_left_triangle) {
						ret += Vector2i(0, -1);
					} else if (in_top_right_triangle) {
						ret += Vector2i(1, 0);
					}
					break;
				case TileSet::TILE_LAYOUT_DIAMOND_DOWN:
					ret = Vector2(ret.x + ret.y / 2, ret.y / 2 - ret.x).floor();
					if (in_top_left_triangle) {
						ret += Vector2i(-1, 0);
					} else if (in_top_right_triangle) {
						ret += Vector2i(0, -1);
					}
					break;
			}
		} else { // TILE_OFFSET_AXIS_VERTICAL
			// Smart floor of the position
			Vector2 raw_pos = ret;
			if (Math::posmod(Math::floor(ret.x), 2) ^ (tile_layout == TileSet::TILE_LAYOUT_STACKED_OFFSET)) {
				ret = Vector2(Math::floor(ret.x), Math::floor(ret.y + 0.5) - 0.5);
			} else {
				ret = ret.floor();
			}

			// Compute the tile offset, and if we might the output for a neighbour top tile
			Vector2 in_tile_pos = raw_pos - ret;
			bool in_top_left_triangle = (in_tile_pos - Vector2(0.0, 0.5)).cross(Vector2(1.0 / overlapping_ratio - 1, -0.5)) > 0;
			bool in_bottom_left_triangle = (in_tile_pos - Vector2(0.0, 0.5)).cross(Vector2(1.0 / overlapping_ratio - 1, 0.5)) <= 0;

			switch (tile_layout) {
				case TileSet::TILE_LAYOUT_STACKED:
					ret = ret.floor();
					if (in_top_left_triangle) {
						ret += Vector2i(-1, Math::posmod(Math::floor(ret.x), 2) ? 0 : -1);
					} else if (in_bottom_left_triangle) {
						ret += Vector2i(-1, Math::posmod(Math::floor(ret.x), 2) ? 1 : 0);
					}
					break;
				case TileSet::TILE_LAYOUT_STACKED_OFFSET:
					ret = ret.floor();
					if (in_top_left_triangle) {
						ret += Vector2i(-1, Math::posmod(Math::floor(ret.x), 2) ? -1 : 0);
					} else if (in_bottom_left_triangle) {
						ret += Vector2i(-1, Math::posmod(Math::floor(ret.x), 2) ? 0 : 1);
					}
					break;
				case TileSet::TILE_LAYOUT_STAIRS_RIGHT:
					ret = Vector2(ret.x / 2 - ret.y, ret.y * 2).floor();
					if (in_top_left_triangle) {
						ret += Vector2i(0, -1);
					} else if (in_bottom_left_triangle) {
						ret += Vector2i(-1, 1);
					}
					break;
				case TileSet::TILE_LAYOUT_STAIRS_DOWN:
					ret = Vector2(ret.x, ret.y - ret.x / 2).floor();
					if (in_top_left_triangle) {
						ret += Vector2i(-1, 0);
					} else if (in_bottom_left_triangle) {
						ret += Vector2i(-1, 1);
					}
					break;
				case TileSet::TILE_LAYOUT_DIAMOND_RIGHT:
					ret = Vector2(ret.x / 2 - ret.y, ret.y + ret.x / 2).floor();
					if (in_top_left_triangle) {
						ret += Vector2i(0, -1);
					} else if (in_bottom_left_triangle) {
						ret += Vector2i(-1, 0);
					}
					break;
				case TileSet::TILE_LAYOUT_DIAMOND_DOWN:
					ret = Vector2(ret.x / 2 + ret.y, ret.y - ret.x / 2).floor();
					if (in_top_left_triangle) {
						ret += Vector2i(-1, 0);
					} else if (in_bottom_left_triangle) {
						ret += Vector2i(0, 1);
					}
					break;
			}
		}
	} else {
		ret = (ret + Vector2(0.00005, 0.00005)).floor();
	}
	return Vector2i(ret);
}

bool TileMap::is_existing_neighbor(TileSet::CellNeighbor p_cell_neighbor) const {
	ERR_FAIL_COND_V(!tile_set.is_valid(), false);

	TileSet::TileShape shape = tile_set->get_tile_shape();
	if (shape == TileSet::TILE_SHAPE_SQUARE) {
		return p_cell_neighbor == TileSet::CELL_NEIGHBOR_RIGHT_SIDE ||
			   p_cell_neighbor == TileSet::CELL_NEIGHBOR_BOTTOM_RIGHT_CORNER ||
			   p_cell_neighbor == TileSet::CELL_NEIGHBOR_BOTTOM_SIDE ||
			   p_cell_neighbor == TileSet::CELL_NEIGHBOR_BOTTOM_LEFT_CORNER ||
			   p_cell_neighbor == TileSet::CELL_NEIGHBOR_LEFT_SIDE ||
			   p_cell_neighbor == TileSet::CELL_NEIGHBOR_TOP_LEFT_CORNER ||
			   p_cell_neighbor == TileSet::CELL_NEIGHBOR_TOP_SIDE ||
			   p_cell_neighbor == TileSet::CELL_NEIGHBOR_TOP_RIGHT_CORNER;

	} else if (shape == TileSet::TILE_SHAPE_ISOMETRIC) {
		return p_cell_neighbor == TileSet::CELL_NEIGHBOR_RIGHT_CORNER ||
			   p_cell_neighbor == TileSet::CELL_NEIGHBOR_BOTTOM_RIGHT_SIDE ||
			   p_cell_neighbor == TileSet::CELL_NEIGHBOR_BOTTOM_CORNER ||
			   p_cell_neighbor == TileSet::CELL_NEIGHBOR_BOTTOM_LEFT_SIDE ||
			   p_cell_neighbor == TileSet::CELL_NEIGHBOR_LEFT_CORNER ||
			   p_cell_neighbor == TileSet::CELL_NEIGHBOR_TOP_LEFT_SIDE ||
			   p_cell_neighbor == TileSet::CELL_NEIGHBOR_TOP_CORNER ||
			   p_cell_neighbor == TileSet::CELL_NEIGHBOR_TOP_RIGHT_SIDE;
	} else {
		if (tile_set->get_tile_offset_axis() == TileSet::TILE_OFFSET_AXIS_HORIZONTAL) {
			return p_cell_neighbor == TileSet::CELL_NEIGHBOR_RIGHT_SIDE ||
				   p_cell_neighbor == TileSet::CELL_NEIGHBOR_BOTTOM_RIGHT_SIDE ||
				   p_cell_neighbor == TileSet::CELL_NEIGHBOR_BOTTOM_LEFT_SIDE ||
				   p_cell_neighbor == TileSet::CELL_NEIGHBOR_LEFT_SIDE ||
				   p_cell_neighbor == TileSet::CELL_NEIGHBOR_TOP_LEFT_SIDE ||
				   p_cell_neighbor == TileSet::CELL_NEIGHBOR_TOP_RIGHT_SIDE;
		} else {
			return p_cell_neighbor == TileSet::CELL_NEIGHBOR_BOTTOM_RIGHT_SIDE ||
				   p_cell_neighbor == TileSet::CELL_NEIGHBOR_BOTTOM_SIDE ||
				   p_cell_neighbor == TileSet::CELL_NEIGHBOR_BOTTOM_LEFT_SIDE ||
				   p_cell_neighbor == TileSet::CELL_NEIGHBOR_TOP_LEFT_SIDE ||
				   p_cell_neighbor == TileSet::CELL_NEIGHBOR_TOP_SIDE ||
				   p_cell_neighbor == TileSet::CELL_NEIGHBOR_TOP_RIGHT_SIDE;
		}
	}
}

Vector2i TileMap::get_neighbor_cell(const Vector2i &p_coords, TileSet::CellNeighbor p_cell_neighbor) const {
	ERR_FAIL_COND_V(!tile_set.is_valid(), p_coords);

	TileSet::TileShape shape = tile_set->get_tile_shape();
	if (shape == TileSet::TILE_SHAPE_SQUARE) {
		switch (p_cell_neighbor) {
			case TileSet::CELL_NEIGHBOR_RIGHT_SIDE:
				return p_coords + Vector2i(1, 0);
			case TileSet::CELL_NEIGHBOR_BOTTOM_RIGHT_CORNER:
				return p_coords + Vector2i(1, 1);
			case TileSet::CELL_NEIGHBOR_BOTTOM_SIDE:
				return p_coords + Vector2i(0, 1);
			case TileSet::CELL_NEIGHBOR_BOTTOM_LEFT_CORNER:
				return p_coords + Vector2i(-1, 1);
			case TileSet::CELL_NEIGHBOR_LEFT_SIDE:
				return p_coords + Vector2i(-1, 0);
			case TileSet::CELL_NEIGHBOR_TOP_LEFT_CORNER:
				return p_coords + Vector2i(-1, -1);
			case TileSet::CELL_NEIGHBOR_TOP_SIDE:
				return p_coords + Vector2i(0, -1);
			case TileSet::CELL_NEIGHBOR_TOP_RIGHT_CORNER:
				return p_coords + Vector2i(1, -1);
			default:
				ERR_FAIL_V(p_coords);
		}
	} else { // Half-offset shapes (square and hexagon)
		switch (tile_set->get_tile_layout()) {
			case TileSet::TILE_LAYOUT_STACKED: {
				if (tile_set->get_tile_offset_axis() == TileSet::TILE_OFFSET_AXIS_HORIZONTAL) {
					bool is_offset = p_coords.y % 2;
					if ((shape == TileSet::TILE_SHAPE_ISOMETRIC && p_cell_neighbor == TileSet::CELL_NEIGHBOR_RIGHT_CORNER) ||
							(shape != TileSet::TILE_SHAPE_ISOMETRIC && p_cell_neighbor == TileSet::CELL_NEIGHBOR_RIGHT_SIDE)) {
						return p_coords + Vector2i(1, 0);
					} else if (p_cell_neighbor == TileSet::CELL_NEIGHBOR_BOTTOM_RIGHT_SIDE) {
						return p_coords + Vector2i(is_offset ? 1 : 0, 1);
					} else if (shape == TileSet::TILE_SHAPE_ISOMETRIC && p_cell_neighbor == TileSet::CELL_NEIGHBOR_BOTTOM_CORNER) {
						return p_coords + Vector2i(0, 2);
					} else if (p_cell_neighbor == TileSet::CELL_NEIGHBOR_BOTTOM_LEFT_SIDE) {
						return p_coords + Vector2i(is_offset ? 0 : -1, 1);
					} else if ((shape == TileSet::TILE_SHAPE_ISOMETRIC && p_cell_neighbor == TileSet::CELL_NEIGHBOR_LEFT_CORNER) ||
							   (shape != TileSet::TILE_SHAPE_ISOMETRIC && p_cell_neighbor == TileSet::CELL_NEIGHBOR_LEFT_SIDE)) {
						return p_coords + Vector2i(-1, 0);
					} else if (p_cell_neighbor == TileSet::CELL_NEIGHBOR_TOP_LEFT_SIDE) {
						return p_coords + Vector2i(is_offset ? 0 : -1, -1);
					} else if (shape == TileSet::TILE_SHAPE_ISOMETRIC && p_cell_neighbor == TileSet::CELL_NEIGHBOR_TOP_CORNER) {
						return p_coords + Vector2i(0, -2);
					} else if (p_cell_neighbor == TileSet::CELL_NEIGHBOR_TOP_RIGHT_SIDE) {
						return p_coords + Vector2i(is_offset ? 1 : 0, -1);
					} else {
						ERR_FAIL_V(p_coords);
					}
				} else {
					bool is_offset = p_coords.x % 2;

					if ((shape == TileSet::TILE_SHAPE_ISOMETRIC && p_cell_neighbor == TileSet::CELL_NEIGHBOR_BOTTOM_CORNER) ||
							(shape != TileSet::TILE_SHAPE_ISOMETRIC && p_cell_neighbor == TileSet::CELL_NEIGHBOR_BOTTOM_SIDE)) {
						return p_coords + Vector2i(0, 1);
					} else if (p_cell_neighbor == TileSet::CELL_NEIGHBOR_BOTTOM_RIGHT_SIDE) {
						return p_coords + Vector2i(1, is_offset ? 1 : 0);
					} else if (shape == TileSet::TILE_SHAPE_ISOMETRIC && p_cell_neighbor == TileSet::CELL_NEIGHBOR_RIGHT_CORNER) {
						return p_coords + Vector2i(2, 0);
					} else if (p_cell_neighbor == TileSet::CELL_NEIGHBOR_TOP_RIGHT_SIDE) {
						return p_coords + Vector2i(1, is_offset ? 0 : -1);
					} else if ((shape == TileSet::TILE_SHAPE_ISOMETRIC && p_cell_neighbor == TileSet::CELL_NEIGHBOR_TOP_CORNER) ||
							   (shape != TileSet::TILE_SHAPE_ISOMETRIC && p_cell_neighbor == TileSet::CELL_NEIGHBOR_TOP_SIDE)) {
						return p_coords + Vector2i(0, -1);
					} else if (p_cell_neighbor == TileSet::CELL_NEIGHBOR_TOP_LEFT_SIDE) {
						return p_coords + Vector2i(-1, is_offset ? 0 : -1);
					} else if (shape == TileSet::TILE_SHAPE_ISOMETRIC && p_cell_neighbor == TileSet::CELL_NEIGHBOR_LEFT_CORNER) {
						return p_coords + Vector2i(-2, 0);
					} else if (p_cell_neighbor == TileSet::CELL_NEIGHBOR_BOTTOM_LEFT_SIDE) {
						return p_coords + Vector2i(-1, is_offset ? 1 : 0);
					} else {
						ERR_FAIL_V(p_coords);
					}
				}
			} break;
			case TileSet::TILE_LAYOUT_STACKED_OFFSET: {
				if (tile_set->get_tile_offset_axis() == TileSet::TILE_OFFSET_AXIS_HORIZONTAL) {
					bool is_offset = p_coords.y % 2;

					if ((shape == TileSet::TILE_SHAPE_ISOMETRIC && p_cell_neighbor == TileSet::CELL_NEIGHBOR_RIGHT_CORNER) ||
							(shape != TileSet::TILE_SHAPE_ISOMETRIC && p_cell_neighbor == TileSet::CELL_NEIGHBOR_RIGHT_SIDE)) {
						return p_coords + Vector2i(1, 0);
					} else if (p_cell_neighbor == TileSet::CELL_NEIGHBOR_BOTTOM_RIGHT_SIDE) {
						return p_coords + Vector2i(is_offset ? 0 : 1, 1);
					} else if (shape == TileSet::TILE_SHAPE_ISOMETRIC && p_cell_neighbor == TileSet::CELL_NEIGHBOR_BOTTOM_CORNER) {
						return p_coords + Vector2i(0, 2);
					} else if (p_cell_neighbor == TileSet::CELL_NEIGHBOR_BOTTOM_LEFT_SIDE) {
						return p_coords + Vector2i(is_offset ? -1 : 0, 1);
					} else if ((shape == TileSet::TILE_SHAPE_ISOMETRIC && p_cell_neighbor == TileSet::CELL_NEIGHBOR_LEFT_CORNER) ||
							   (shape != TileSet::TILE_SHAPE_ISOMETRIC && p_cell_neighbor == TileSet::CELL_NEIGHBOR_LEFT_SIDE)) {
						return p_coords + Vector2i(-1, 0);
					} else if (p_cell_neighbor == TileSet::CELL_NEIGHBOR_TOP_LEFT_SIDE) {
						return p_coords + Vector2i(is_offset ? -1 : 0, -1);
					} else if (shape == TileSet::TILE_SHAPE_ISOMETRIC && p_cell_neighbor == TileSet::CELL_NEIGHBOR_TOP_CORNER) {
						return p_coords + Vector2i(0, -2);
					} else if (p_cell_neighbor == TileSet::CELL_NEIGHBOR_TOP_RIGHT_SIDE) {
						return p_coords + Vector2i(is_offset ? 0 : 1, -1);
					} else {
						ERR_FAIL_V(p_coords);
					}
				} else {
					bool is_offset = p_coords.x % 2;

					if ((shape == TileSet::TILE_SHAPE_ISOMETRIC && p_cell_neighbor == TileSet::CELL_NEIGHBOR_BOTTOM_CORNER) ||
							(shape != TileSet::TILE_SHAPE_ISOMETRIC && p_cell_neighbor == TileSet::CELL_NEIGHBOR_BOTTOM_SIDE)) {
						return p_coords + Vector2i(0, 1);
					} else if (p_cell_neighbor == TileSet::CELL_NEIGHBOR_BOTTOM_RIGHT_SIDE) {
						return p_coords + Vector2i(1, is_offset ? 0 : 1);
					} else if (shape == TileSet::TILE_SHAPE_ISOMETRIC && p_cell_neighbor == TileSet::CELL_NEIGHBOR_RIGHT_CORNER) {
						return p_coords + Vector2i(2, 0);
					} else if (p_cell_neighbor == TileSet::CELL_NEIGHBOR_TOP_RIGHT_SIDE) {
						return p_coords + Vector2i(1, is_offset ? -1 : 0);
					} else if ((shape == TileSet::TILE_SHAPE_ISOMETRIC && p_cell_neighbor == TileSet::CELL_NEIGHBOR_TOP_CORNER) ||
							   (shape != TileSet::TILE_SHAPE_ISOMETRIC && p_cell_neighbor == TileSet::CELL_NEIGHBOR_TOP_SIDE)) {
						return p_coords + Vector2i(0, -1);
					} else if (p_cell_neighbor == TileSet::CELL_NEIGHBOR_TOP_LEFT_SIDE) {
						return p_coords + Vector2i(-1, is_offset ? -1 : 0);
					} else if (shape == TileSet::TILE_SHAPE_ISOMETRIC && p_cell_neighbor == TileSet::CELL_NEIGHBOR_LEFT_CORNER) {
						return p_coords + Vector2i(-2, 0);
					} else if (p_cell_neighbor == TileSet::CELL_NEIGHBOR_BOTTOM_LEFT_SIDE) {
						return p_coords + Vector2i(-1, is_offset ? 0 : 1);
					} else {
						ERR_FAIL_V(p_coords);
					}
				}
			} break;
			case TileSet::TILE_LAYOUT_STAIRS_RIGHT:
			case TileSet::TILE_LAYOUT_STAIRS_DOWN: {
				if ((tile_set->get_tile_layout() == TileSet::TILE_LAYOUT_STAIRS_RIGHT) ^ (tile_set->get_tile_offset_axis() == TileSet::TILE_OFFSET_AXIS_VERTICAL)) {
					if (tile_set->get_tile_offset_axis() == TileSet::TILE_OFFSET_AXIS_HORIZONTAL) {
						if ((shape == TileSet::TILE_SHAPE_ISOMETRIC && p_cell_neighbor == TileSet::CELL_NEIGHBOR_RIGHT_CORNER) ||
								(shape != TileSet::TILE_SHAPE_ISOMETRIC && p_cell_neighbor == TileSet::CELL_NEIGHBOR_RIGHT_SIDE)) {
							return p_coords + Vector2i(1, 0);
						} else if (p_cell_neighbor == TileSet::CELL_NEIGHBOR_BOTTOM_RIGHT_SIDE) {
							return p_coords + Vector2i(0, 1);
						} else if (shape == TileSet::TILE_SHAPE_ISOMETRIC && p_cell_neighbor == TileSet::CELL_NEIGHBOR_BOTTOM_CORNER) {
							return p_coords + Vector2i(-1, 2);
						} else if (p_cell_neighbor == TileSet::CELL_NEIGHBOR_BOTTOM_LEFT_SIDE) {
							return p_coords + Vector2i(-1, 1);
						} else if ((shape == TileSet::TILE_SHAPE_ISOMETRIC && p_cell_neighbor == TileSet::CELL_NEIGHBOR_LEFT_CORNER) ||
								   (shape != TileSet::TILE_SHAPE_ISOMETRIC && p_cell_neighbor == TileSet::CELL_NEIGHBOR_LEFT_SIDE)) {
							return p_coords + Vector2i(-1, 0);
						} else if (p_cell_neighbor == TileSet::CELL_NEIGHBOR_TOP_LEFT_SIDE) {
							return p_coords + Vector2i(0, -1);
						} else if (shape == TileSet::TILE_SHAPE_ISOMETRIC && p_cell_neighbor == TileSet::CELL_NEIGHBOR_TOP_CORNER) {
							return p_coords + Vector2i(1, -2);
						} else if (p_cell_neighbor == TileSet::CELL_NEIGHBOR_TOP_RIGHT_SIDE) {
							return p_coords + Vector2i(1, -1);
						} else {
							ERR_FAIL_V(p_coords);
						}

					} else {
						if ((shape == TileSet::TILE_SHAPE_ISOMETRIC && p_cell_neighbor == TileSet::CELL_NEIGHBOR_BOTTOM_CORNER) ||
								(shape != TileSet::TILE_SHAPE_ISOMETRIC && p_cell_neighbor == TileSet::CELL_NEIGHBOR_BOTTOM_SIDE)) {
							return p_coords + Vector2i(0, 1);
						} else if (p_cell_neighbor == TileSet::CELL_NEIGHBOR_BOTTOM_RIGHT_SIDE) {
							return p_coords + Vector2i(1, 0);
						} else if (shape == TileSet::TILE_SHAPE_ISOMETRIC && p_cell_neighbor == TileSet::CELL_NEIGHBOR_RIGHT_CORNER) {
							return p_coords + Vector2i(2, -1);
						} else if (p_cell_neighbor == TileSet::CELL_NEIGHBOR_TOP_RIGHT_SIDE) {
							return p_coords + Vector2i(1, -1);
						} else if ((shape == TileSet::TILE_SHAPE_ISOMETRIC && p_cell_neighbor == TileSet::CELL_NEIGHBOR_TOP_CORNER) ||
								   (shape != TileSet::TILE_SHAPE_ISOMETRIC && p_cell_neighbor == TileSet::CELL_NEIGHBOR_TOP_SIDE)) {
							return p_coords + Vector2i(0, -1);
						} else if (p_cell_neighbor == TileSet::CELL_NEIGHBOR_TOP_LEFT_SIDE) {
							return p_coords + Vector2i(-1, 0);
						} else if (shape == TileSet::TILE_SHAPE_ISOMETRIC && p_cell_neighbor == TileSet::CELL_NEIGHBOR_LEFT_CORNER) {
							return p_coords + Vector2i(-2, 1);

						} else if (p_cell_neighbor == TileSet::CELL_NEIGHBOR_BOTTOM_LEFT_SIDE) {
							return p_coords + Vector2i(-1, 1);
						} else {
							ERR_FAIL_V(p_coords);
						}
					}
				} else {
					if (tile_set->get_tile_offset_axis() == TileSet::TILE_OFFSET_AXIS_HORIZONTAL) {
						if ((shape == TileSet::TILE_SHAPE_ISOMETRIC && p_cell_neighbor == TileSet::CELL_NEIGHBOR_RIGHT_CORNER) ||
								(shape != TileSet::TILE_SHAPE_ISOMETRIC && p_cell_neighbor == TileSet::CELL_NEIGHBOR_RIGHT_SIDE)) {
							return p_coords + Vector2i(2, -1);
						} else if (p_cell_neighbor == TileSet::CELL_NEIGHBOR_BOTTOM_RIGHT_SIDE) {
							return p_coords + Vector2i(1, 0);
						} else if (shape == TileSet::TILE_SHAPE_ISOMETRIC && p_cell_neighbor == TileSet::CELL_NEIGHBOR_BOTTOM_CORNER) {
							return p_coords + Vector2i(0, 1);
						} else if (p_cell_neighbor == TileSet::CELL_NEIGHBOR_BOTTOM_LEFT_SIDE) {
							return p_coords + Vector2i(-1, 1);
						} else if ((shape == TileSet::TILE_SHAPE_ISOMETRIC && p_cell_neighbor == TileSet::CELL_NEIGHBOR_LEFT_CORNER) ||
								   (shape != TileSet::TILE_SHAPE_ISOMETRIC && p_cell_neighbor == TileSet::CELL_NEIGHBOR_LEFT_SIDE)) {
							return p_coords + Vector2i(-2, 1);
						} else if (p_cell_neighbor == TileSet::CELL_NEIGHBOR_TOP_LEFT_SIDE) {
							return p_coords + Vector2i(-1, 0);
						} else if (shape == TileSet::TILE_SHAPE_ISOMETRIC && p_cell_neighbor == TileSet::CELL_NEIGHBOR_TOP_CORNER) {
							return p_coords + Vector2i(0, -1);
						} else if (p_cell_neighbor == TileSet::CELL_NEIGHBOR_TOP_RIGHT_SIDE) {
							return p_coords + Vector2i(1, -1);
						} else {
							ERR_FAIL_V(p_coords);
						}

					} else {
						if ((shape == TileSet::TILE_SHAPE_ISOMETRIC && p_cell_neighbor == TileSet::CELL_NEIGHBOR_BOTTOM_CORNER) ||
								(shape != TileSet::TILE_SHAPE_ISOMETRIC && p_cell_neighbor == TileSet::CELL_NEIGHBOR_BOTTOM_SIDE)) {
							return p_coords + Vector2i(-1, 2);
						} else if (p_cell_neighbor == TileSet::CELL_NEIGHBOR_BOTTOM_RIGHT_SIDE) {
							return p_coords + Vector2i(0, 1);
						} else if (shape == TileSet::TILE_SHAPE_ISOMETRIC && p_cell_neighbor == TileSet::CELL_NEIGHBOR_RIGHT_CORNER) {
							return p_coords + Vector2i(1, 0);
						} else if (p_cell_neighbor == TileSet::CELL_NEIGHBOR_TOP_RIGHT_SIDE) {
							return p_coords + Vector2i(1, -1);
						} else if ((shape == TileSet::TILE_SHAPE_ISOMETRIC && p_cell_neighbor == TileSet::CELL_NEIGHBOR_TOP_CORNER) ||
								   (shape != TileSet::TILE_SHAPE_ISOMETRIC && p_cell_neighbor == TileSet::CELL_NEIGHBOR_TOP_SIDE)) {
							return p_coords + Vector2i(1, -2);
						} else if (p_cell_neighbor == TileSet::CELL_NEIGHBOR_TOP_LEFT_SIDE) {
							return p_coords + Vector2i(0, -1);
						} else if (shape == TileSet::TILE_SHAPE_ISOMETRIC && p_cell_neighbor == TileSet::CELL_NEIGHBOR_LEFT_CORNER) {
							return p_coords + Vector2i(-1, 0);

						} else if (p_cell_neighbor == TileSet::CELL_NEIGHBOR_BOTTOM_LEFT_SIDE) {
							return p_coords + Vector2i(-1, 1);
						} else {
							ERR_FAIL_V(p_coords);
						}
					}
				}
			} break;
			case TileSet::TILE_LAYOUT_DIAMOND_RIGHT:
			case TileSet::TILE_LAYOUT_DIAMOND_DOWN: {
				if ((tile_set->get_tile_layout() == TileSet::TILE_LAYOUT_DIAMOND_RIGHT) ^ (tile_set->get_tile_offset_axis() == TileSet::TILE_OFFSET_AXIS_VERTICAL)) {
					if (tile_set->get_tile_offset_axis() == TileSet::TILE_OFFSET_AXIS_HORIZONTAL) {
						if ((shape == TileSet::TILE_SHAPE_ISOMETRIC && p_cell_neighbor == TileSet::CELL_NEIGHBOR_RIGHT_CORNER) ||
								(shape != TileSet::TILE_SHAPE_ISOMETRIC && p_cell_neighbor == TileSet::CELL_NEIGHBOR_RIGHT_SIDE)) {
							return p_coords + Vector2i(1, 1);
						} else if (p_cell_neighbor == TileSet::CELL_NEIGHBOR_BOTTOM_RIGHT_SIDE) {
							return p_coords + Vector2i(0, 1);
						} else if (shape == TileSet::TILE_SHAPE_ISOMETRIC && p_cell_neighbor == TileSet::CELL_NEIGHBOR_BOTTOM_CORNER) {
							return p_coords + Vector2i(-1, 1);
						} else if (p_cell_neighbor == TileSet::CELL_NEIGHBOR_BOTTOM_LEFT_SIDE) {
							return p_coords + Vector2i(-1, 0);
						} else if ((shape == TileSet::TILE_SHAPE_ISOMETRIC && p_cell_neighbor == TileSet::CELL_NEIGHBOR_LEFT_CORNER) ||
								   (shape != TileSet::TILE_SHAPE_ISOMETRIC && p_cell_neighbor == TileSet::CELL_NEIGHBOR_LEFT_SIDE)) {
							return p_coords + Vector2i(-1, -1);
						} else if (p_cell_neighbor == TileSet::CELL_NEIGHBOR_TOP_LEFT_SIDE) {
							return p_coords + Vector2i(0, -1);
						} else if (shape == TileSet::TILE_SHAPE_ISOMETRIC && p_cell_neighbor == TileSet::CELL_NEIGHBOR_TOP_CORNER) {
							return p_coords + Vector2i(1, -1);
						} else if (p_cell_neighbor == TileSet::CELL_NEIGHBOR_TOP_RIGHT_SIDE) {
							return p_coords + Vector2i(1, 0);
						} else {
							ERR_FAIL_V(p_coords);
						}

					} else {
						if ((shape == TileSet::TILE_SHAPE_ISOMETRIC && p_cell_neighbor == TileSet::CELL_NEIGHBOR_BOTTOM_CORNER) ||
								(shape != TileSet::TILE_SHAPE_ISOMETRIC && p_cell_neighbor == TileSet::CELL_NEIGHBOR_BOTTOM_SIDE)) {
							return p_coords + Vector2i(1, 1);
						} else if (p_cell_neighbor == TileSet::CELL_NEIGHBOR_BOTTOM_RIGHT_SIDE) {
							return p_coords + Vector2i(1, 0);
						} else if (shape == TileSet::TILE_SHAPE_ISOMETRIC && p_cell_neighbor == TileSet::CELL_NEIGHBOR_RIGHT_CORNER) {
							return p_coords + Vector2i(1, -1);
						} else if (p_cell_neighbor == TileSet::CELL_NEIGHBOR_TOP_RIGHT_SIDE) {
							return p_coords + Vector2i(0, -1);
						} else if ((shape == TileSet::TILE_SHAPE_ISOMETRIC && p_cell_neighbor == TileSet::CELL_NEIGHBOR_TOP_CORNER) ||
								   (shape != TileSet::TILE_SHAPE_ISOMETRIC && p_cell_neighbor == TileSet::CELL_NEIGHBOR_TOP_SIDE)) {
							return p_coords + Vector2i(-1, -1);
						} else if (p_cell_neighbor == TileSet::CELL_NEIGHBOR_TOP_LEFT_SIDE) {
							return p_coords + Vector2i(-1, 0);
						} else if (shape == TileSet::TILE_SHAPE_ISOMETRIC && p_cell_neighbor == TileSet::CELL_NEIGHBOR_LEFT_CORNER) {
							return p_coords + Vector2i(-1, 1);

						} else if (p_cell_neighbor == TileSet::CELL_NEIGHBOR_BOTTOM_LEFT_SIDE) {
							return p_coords + Vector2i(0, 1);
						} else {
							ERR_FAIL_V(p_coords);
						}
					}
				} else {
					if (tile_set->get_tile_offset_axis() == TileSet::TILE_OFFSET_AXIS_HORIZONTAL) {
						if ((shape == TileSet::TILE_SHAPE_ISOMETRIC && p_cell_neighbor == TileSet::CELL_NEIGHBOR_RIGHT_CORNER) ||
								(shape != TileSet::TILE_SHAPE_ISOMETRIC && p_cell_neighbor == TileSet::CELL_NEIGHBOR_RIGHT_SIDE)) {
							return p_coords + Vector2i(1, -1);
						} else if (p_cell_neighbor == TileSet::CELL_NEIGHBOR_BOTTOM_RIGHT_SIDE) {
							return p_coords + Vector2i(1, 0);
						} else if (shape == TileSet::TILE_SHAPE_ISOMETRIC && p_cell_neighbor == TileSet::CELL_NEIGHBOR_BOTTOM_CORNER) {
							return p_coords + Vector2i(1, 1);
						} else if (p_cell_neighbor == TileSet::CELL_NEIGHBOR_BOTTOM_LEFT_SIDE) {
							return p_coords + Vector2i(0, 1);
						} else if ((shape == TileSet::TILE_SHAPE_ISOMETRIC && p_cell_neighbor == TileSet::CELL_NEIGHBOR_LEFT_CORNER) ||
								   (shape != TileSet::TILE_SHAPE_ISOMETRIC && p_cell_neighbor == TileSet::CELL_NEIGHBOR_LEFT_SIDE)) {
							return p_coords + Vector2i(-1, 1);
						} else if (p_cell_neighbor == TileSet::CELL_NEIGHBOR_TOP_LEFT_SIDE) {
							return p_coords + Vector2i(-1, 0);
						} else if (shape == TileSet::TILE_SHAPE_ISOMETRIC && p_cell_neighbor == TileSet::CELL_NEIGHBOR_TOP_CORNER) {
							return p_coords + Vector2i(-1, -1);
						} else if (p_cell_neighbor == TileSet::CELL_NEIGHBOR_TOP_RIGHT_SIDE) {
							return p_coords + Vector2i(0, -1);
						} else {
							ERR_FAIL_V(p_coords);
						}

					} else {
						if ((shape == TileSet::TILE_SHAPE_ISOMETRIC && p_cell_neighbor == TileSet::CELL_NEIGHBOR_BOTTOM_CORNER) ||
								(shape != TileSet::TILE_SHAPE_ISOMETRIC && p_cell_neighbor == TileSet::CELL_NEIGHBOR_BOTTOM_SIDE)) {
							return p_coords + Vector2i(-1, 1);
						} else if (p_cell_neighbor == TileSet::CELL_NEIGHBOR_BOTTOM_RIGHT_SIDE) {
							return p_coords + Vector2i(0, 1);
						} else if (shape == TileSet::TILE_SHAPE_ISOMETRIC && p_cell_neighbor == TileSet::CELL_NEIGHBOR_RIGHT_CORNER) {
							return p_coords + Vector2i(1, 1);
						} else if (p_cell_neighbor == TileSet::CELL_NEIGHBOR_TOP_RIGHT_SIDE) {
							return p_coords + Vector2i(1, 0);
						} else if ((shape == TileSet::TILE_SHAPE_ISOMETRIC && p_cell_neighbor == TileSet::CELL_NEIGHBOR_TOP_CORNER) ||
								   (shape != TileSet::TILE_SHAPE_ISOMETRIC && p_cell_neighbor == TileSet::CELL_NEIGHBOR_TOP_SIDE)) {
							return p_coords + Vector2i(1, -1);
						} else if (p_cell_neighbor == TileSet::CELL_NEIGHBOR_TOP_LEFT_SIDE) {
							return p_coords + Vector2i(0, -1);
						} else if (shape == TileSet::TILE_SHAPE_ISOMETRIC && p_cell_neighbor == TileSet::CELL_NEIGHBOR_LEFT_CORNER) {
							return p_coords + Vector2i(-1, -1);

						} else if (p_cell_neighbor == TileSet::CELL_NEIGHBOR_BOTTOM_LEFT_SIDE) {
							return p_coords + Vector2i(-1, 0);
						} else {
							ERR_FAIL_V(p_coords);
						}
					}
				}
			} break;
			default:
				ERR_FAIL_V(p_coords);
		}
	}
}

TypedArray<Vector2i> TileMap::get_used_cells(int p_layer) const {
	ERR_FAIL_INDEX_V(p_layer, (int)layers.size(), TypedArray<Vector2i>());

	// Returns the cells used in the tilemap.
	TypedArray<Vector2i> a;
	a.resize(layers[p_layer].tile_map.size());
	int i = 0;
	for (Map<Vector2i, TileMapCell>::Element *E = layers[p_layer].tile_map.front(); E; E = E->next()) {
		Vector2i p(E->key().x, E->key().y);
		a[i++] = p;
	}

	return a;
}

Rect2 TileMap::get_used_rect() { // Not const because of cache
	// Return the rect of the currently used area
	if (used_rect_cache_dirty) {
		bool first = true;
		used_rect_cache = Rect2i();

		for (unsigned int i = 0; i < layers.size(); i++) {
			const Map<Vector2i, TileMapCell> &tile_map = layers[i].tile_map;
			if (tile_map.size() > 0) {
				if (first) {
					used_rect_cache = Rect2i(tile_map.front()->key().x, tile_map.front()->key().y, 0, 0);
					first = false;
				}

				for (Map<Vector2i, TileMapCell>::Element *E = tile_map.front(); E; E = E->next()) {
					used_rect_cache.expand_to(Vector2i(E->key().x, E->key().y));
				}
			}
		}

		if (!first) { // first is true if every layer is empty.
			used_rect_cache.size += Vector2i(1, 1); // The cache expands to top-left coordinate, so we add one full tile.
		}
		used_rect_cache_dirty = false;
	}

	return used_rect_cache;
}

// --- Override some methods of the CanvasItem class to pass the changes to the quadrants CanvasItems ---

void TileMap::set_light_mask(int p_light_mask) {
	// Occlusion: set light mask.
	CanvasItem::set_light_mask(p_light_mask);
	for (unsigned int layer = 0; layer < layers.size(); layer++) {
		for (Map<Vector2i, TileMapQuadrant>::Element *E = layers[layer].quadrant_map.front(); E; E = E->next()) {
			for (const RID &ci : E->get().canvas_items) {
				RenderingServer::get_singleton()->canvas_item_set_light_mask(ci, get_light_mask());
			}
		}
		_rendering_update_layer(layer);
	}
}

void TileMap::set_material(const Ref<Material> &p_material) {
	// Set material for the whole tilemap.
	CanvasItem::set_material(p_material);

	// Update material for the whole tilemap.
	for (unsigned int layer = 0; layer < layers.size(); layer++) {
		for (Map<Vector2i, TileMapQuadrant>::Element *E = layers[layer].quadrant_map.front(); E; E = E->next()) {
			TileMapQuadrant &q = E->get();
			for (const RID &ci : q.canvas_items) {
				RS::get_singleton()->canvas_item_set_use_parent_material(ci, get_use_parent_material() || get_material().is_valid());
			}
		}
		_rendering_update_layer(layer);
	}
}

void TileMap::set_use_parent_material(bool p_use_parent_material) {
	// Set use_parent_material for the whole tilemap.
	CanvasItem::set_use_parent_material(p_use_parent_material);

	// Update use_parent_material for the whole tilemap.
	for (unsigned int layer = 0; layer < layers.size(); layer++) {
		for (Map<Vector2i, TileMapQuadrant>::Element *E = layers[layer].quadrant_map.front(); E; E = E->next()) {
			TileMapQuadrant &q = E->get();
			for (const RID &ci : q.canvas_items) {
				RS::get_singleton()->canvas_item_set_use_parent_material(ci, get_use_parent_material() || get_material().is_valid());
			}
		}
		_rendering_update_layer(layer);
	}
}

void TileMap::set_texture_filter(TextureFilter p_texture_filter) {
	// Set a default texture filter for the whole tilemap
	CanvasItem::set_texture_filter(p_texture_filter);
	for (unsigned int layer = 0; layer < layers.size(); layer++) {
		for (Map<Vector2i, TileMapQuadrant>::Element *F = layers[layer].quadrant_map.front(); F; F = F->next()) {
			TileMapQuadrant &q = F->get();
			for (const RID &ci : q.canvas_items) {
				RenderingServer::get_singleton()->canvas_item_set_default_texture_filter(ci, RS::CanvasItemTextureFilter(p_texture_filter));
				_make_quadrant_dirty(F);
			}
		}
		_rendering_update_layer(layer);
	}
}

void TileMap::set_texture_repeat(CanvasItem::TextureRepeat p_texture_repeat) {
	// Set a default texture repeat for the whole tilemap
	CanvasItem::set_texture_repeat(p_texture_repeat);
	for (unsigned int layer = 0; layer < layers.size(); layer++) {
		for (Map<Vector2i, TileMapQuadrant>::Element *F = layers[layer].quadrant_map.front(); F; F = F->next()) {
			TileMapQuadrant &q = F->get();
			for (const RID &ci : q.canvas_items) {
				RenderingServer::get_singleton()->canvas_item_set_default_texture_repeat(ci, RS::CanvasItemTextureRepeat(p_texture_repeat));
				_make_quadrant_dirty(F);
			}
		}
		_rendering_update_layer(layer);
	}
}

TypedArray<Vector2i> TileMap::get_surrounding_tiles(Vector2i coords) {
	if (!tile_set.is_valid()) {
		return TypedArray<Vector2i>();
	}

	TypedArray<Vector2i> around;
	TileSet::TileShape shape = tile_set->get_tile_shape();
	if (shape == TileSet::TILE_SHAPE_SQUARE) {
		around.push_back(get_neighbor_cell(coords, TileSet::CELL_NEIGHBOR_RIGHT_SIDE));
		around.push_back(get_neighbor_cell(coords, TileSet::CELL_NEIGHBOR_BOTTOM_SIDE));
		around.push_back(get_neighbor_cell(coords, TileSet::CELL_NEIGHBOR_LEFT_SIDE));
		around.push_back(get_neighbor_cell(coords, TileSet::CELL_NEIGHBOR_TOP_SIDE));
	} else if (shape == TileSet::TILE_SHAPE_ISOMETRIC) {
		around.push_back(get_neighbor_cell(coords, TileSet::CELL_NEIGHBOR_BOTTOM_RIGHT_SIDE));
		around.push_back(get_neighbor_cell(coords, TileSet::CELL_NEIGHBOR_BOTTOM_LEFT_SIDE));
		around.push_back(get_neighbor_cell(coords, TileSet::CELL_NEIGHBOR_TOP_LEFT_SIDE));
		around.push_back(get_neighbor_cell(coords, TileSet::CELL_NEIGHBOR_TOP_RIGHT_SIDE));
	} else {
		if (tile_set->get_tile_offset_axis() == TileSet::TILE_OFFSET_AXIS_HORIZONTAL) {
			around.push_back(get_neighbor_cell(coords, TileSet::CELL_NEIGHBOR_RIGHT_SIDE));
			around.push_back(get_neighbor_cell(coords, TileSet::CELL_NEIGHBOR_BOTTOM_RIGHT_SIDE));
			around.push_back(get_neighbor_cell(coords, TileSet::CELL_NEIGHBOR_BOTTOM_LEFT_SIDE));
			around.push_back(get_neighbor_cell(coords, TileSet::CELL_NEIGHBOR_LEFT_SIDE));
			around.push_back(get_neighbor_cell(coords, TileSet::CELL_NEIGHBOR_TOP_LEFT_SIDE));
			around.push_back(get_neighbor_cell(coords, TileSet::CELL_NEIGHBOR_TOP_RIGHT_SIDE));
		} else {
			around.push_back(get_neighbor_cell(coords, TileSet::CELL_NEIGHBOR_BOTTOM_RIGHT_SIDE));
			around.push_back(get_neighbor_cell(coords, TileSet::CELL_NEIGHBOR_BOTTOM_SIDE));
			around.push_back(get_neighbor_cell(coords, TileSet::CELL_NEIGHBOR_BOTTOM_LEFT_SIDE));
			around.push_back(get_neighbor_cell(coords, TileSet::CELL_NEIGHBOR_TOP_LEFT_SIDE));
			around.push_back(get_neighbor_cell(coords, TileSet::CELL_NEIGHBOR_TOP_SIDE));
			around.push_back(get_neighbor_cell(coords, TileSet::CELL_NEIGHBOR_TOP_RIGHT_SIDE));
		}
	}

	return around;
}

void TileMap::draw_cells_outline(Control *p_control, Set<Vector2i> p_cells, Color p_color, Transform2D p_transform) {
	if (!tile_set.is_valid()) {
		return;
	}

	// Create a set.
	Vector2i tile_size = tile_set->get_tile_size();
	Vector<Vector2> uvs;

	if (tile_set->get_tile_shape() == TileSet::TILE_SHAPE_SQUARE) {
		uvs.append(Vector2(1.0, 0.0));
		uvs.append(Vector2(1.0, 1.0));
		uvs.append(Vector2(0.0, 1.0));
		uvs.append(Vector2(0.0, 0.0));
	} else {
		float overlap = 0.0;
		switch (tile_set->get_tile_shape()) {
			case TileSet::TILE_SHAPE_ISOMETRIC:
				overlap = 0.5;
				break;
			case TileSet::TILE_SHAPE_HEXAGON:
				overlap = 0.25;
				break;
			case TileSet::TILE_SHAPE_HALF_OFFSET_SQUARE:
				overlap = 0.0;
				break;
			default:
				break;
		}
		uvs.append(Vector2(1.0, overlap));
		uvs.append(Vector2(1.0, 1.0 - overlap));
		uvs.append(Vector2(0.5, 1.0));
		uvs.append(Vector2(0.0, 1.0 - overlap));
		uvs.append(Vector2(0.0, overlap));
		uvs.append(Vector2(0.5, 0.0));
		if (tile_set->get_tile_offset_axis() == TileSet::TILE_OFFSET_AXIS_VERTICAL) {
			for (int i = 0; i < uvs.size(); i++) {
				uvs.write[i] = Vector2(uvs[i].y, uvs[i].x);
			}
		}
	}

	for (Set<Vector2i>::Element *E = p_cells.front(); E; E = E->next()) {
		Vector2 top_left = map_to_world(E->get()) - tile_size / 2;
		TypedArray<Vector2i> surrounding_tiles = get_surrounding_tiles(E->get());
		for (int i = 0; i < surrounding_tiles.size(); i++) {
			if (!p_cells.has(surrounding_tiles[i])) {
				p_control->draw_line(p_transform.xform(top_left + uvs[i] * tile_size), p_transform.xform(top_left + uvs[(i + 1) % uvs.size()] * tile_size), p_color);
			}
		}
	}
}

TypedArray<String> TileMap::get_configuration_warnings() const {
	TypedArray<String> warnings = Node::get_configuration_warnings();

	// Retrieve the set of Z index values with a Y-sorted layer.
	Set<int> y_sorted_z_index;
	for (int layer = 0; layer < (int)layers.size(); layer++) {
		if (layers[layer].y_sort_enabled) {
			y_sorted_z_index.insert(layers[layer].z_index);
		}
	}

	// Check if we have a non-sorted layer in a Z-index with a Y-sorted layer.
	for (int layer = 0; layer < (int)layers.size(); layer++) {
		if (!layers[layer].y_sort_enabled && y_sorted_z_index.has(layers[layer].z_index)) {
			warnings.push_back(TTR("A Y-sorted layer has the same Z-index value as a not Y-sorted layer.\nThis may lead to unwanted behaviors, as a layer that is not Y-sorted will be Y-sorted as a whole with tiles from Y-sorted layers."));
			break;
		}
	}

	return warnings;
}

void TileMap::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_tileset", "tileset"), &TileMap::set_tileset);
	ClassDB::bind_method(D_METHOD("get_tileset"), &TileMap::get_tileset);

	ClassDB::bind_method(D_METHOD("set_quadrant_size", "size"), &TileMap::set_quadrant_size);
	ClassDB::bind_method(D_METHOD("get_quadrant_size"), &TileMap::get_quadrant_size);

	ClassDB::bind_method(D_METHOD("set_layers_count", "layers_count"), &TileMap::set_layers_count);
	ClassDB::bind_method(D_METHOD("get_layers_count"), &TileMap::get_layers_count);
	ClassDB::bind_method(D_METHOD("set_layer_name", "layer", "name"), &TileMap::set_layer_name);
	ClassDB::bind_method(D_METHOD("get_layer_name", "layer"), &TileMap::get_layer_name);
	ClassDB::bind_method(D_METHOD("set_layer_enabled", "layer", "enabled"), &TileMap::set_layer_enabled);
	ClassDB::bind_method(D_METHOD("is_layer_enabled", "layer"), &TileMap::is_layer_enabled);
	ClassDB::bind_method(D_METHOD("set_layer_y_sort_enabled", "layer", "y_sort_enabled"), &TileMap::set_layer_y_sort_enabled);
	ClassDB::bind_method(D_METHOD("is_layer_y_sort_enabled", "layer"), &TileMap::is_layer_y_sort_enabled);
	ClassDB::bind_method(D_METHOD("set_layer_y_sort_origin", "layer", "y_sort_origin"), &TileMap::set_layer_y_sort_origin);
	ClassDB::bind_method(D_METHOD("get_layer_y_sort_origin", "layer"), &TileMap::get_layer_y_sort_origin);
	ClassDB::bind_method(D_METHOD("set_layer_z_index", "layer", "z_index"), &TileMap::set_layer_z_index);
	ClassDB::bind_method(D_METHOD("get_layer_z_indexd", "layer"), &TileMap::get_layer_z_index);

	ClassDB::bind_method(D_METHOD("set_collision_visibility_mode", "collision_visibility_mode"), &TileMap::set_collision_visibility_mode);
	ClassDB::bind_method(D_METHOD("get_collision_visibility_mode"), &TileMap::get_collision_visibility_mode);

	ClassDB::bind_method(D_METHOD("set_navigation_visibility_mode", "navigation_visibility_mode"), &TileMap::set_navigation_visibility_mode);
	ClassDB::bind_method(D_METHOD("get_navigation_visibility_mode"), &TileMap::get_navigation_visibility_mode);

	ClassDB::bind_method(D_METHOD("set_cell", "layer", "coords", "source_id", "atlas_coords", "alternative_tile"), &TileMap::set_cell, DEFVAL(TileSet::INVALID_SOURCE), DEFVAL(TileSetSource::INVALID_ATLAS_COORDS), DEFVAL(TileSetSource::INVALID_TILE_ALTERNATIVE));
	ClassDB::bind_method(D_METHOD("get_cell_source_id", "layer", "coords", "use_proxies"), &TileMap::get_cell_source_id);
	ClassDB::bind_method(D_METHOD("get_cell_atlas_coords", "layer", "coords", "use_proxies"), &TileMap::get_cell_atlas_coords);
	ClassDB::bind_method(D_METHOD("get_cell_alternative_tile", "layer", "coords", "use_proxies"), &TileMap::get_cell_alternative_tile);

	ClassDB::bind_method(D_METHOD("fix_invalid_tiles"), &TileMap::fix_invalid_tiles);
	ClassDB::bind_method(D_METHOD("get_surrounding_tiles", "coords"), &TileMap::get_surrounding_tiles);
	ClassDB::bind_method(D_METHOD("clear"), &TileMap::clear);

	ClassDB::bind_method(D_METHOD("get_used_cells", "layer"), &TileMap::get_used_cells);
	ClassDB::bind_method(D_METHOD("get_used_rect"), &TileMap::get_used_rect);

	ClassDB::bind_method(D_METHOD("map_to_world", "map_position"), &TileMap::map_to_world);
	ClassDB::bind_method(D_METHOD("world_to_map", "world_position"), &TileMap::world_to_map);

	ClassDB::bind_method(D_METHOD("get_neighbor_cell", "coords", "neighbor"), &TileMap::get_neighbor_cell);

	ClassDB::bind_method(D_METHOD("_update_dirty_quadrants"), &TileMap::_update_dirty_quadrants);

	ClassDB::bind_method(D_METHOD("_set_tile_data", "layer"), &TileMap::_set_tile_data);
	ClassDB::bind_method(D_METHOD("_get_tile_data", "layer"), &TileMap::_get_tile_data);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "tile_set", PROPERTY_HINT_RESOURCE_TYPE, "TileSet"), "set_tileset", "get_tileset");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "cell_quadrant_size", PROPERTY_HINT_RANGE, "1,128,1"), "set_quadrant_size", "get_quadrant_size");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "collision_visibility_mode", PROPERTY_HINT_ENUM, "Default,Force Show,Force Hide"), "set_collision_visibility_mode", "get_collision_visibility_mode");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "navigation_visibility_mode", PROPERTY_HINT_ENUM, "Default,Force Show,Force Hide"), "set_navigation_visibility_mode", "get_navigation_visibility_mode");

	ADD_GROUP("Layers", "");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "layers_count"), "set_layers_count", "get_layers_count");
	ADD_PROPERTY_DEFAULT("layers_count", 1);

	ADD_PROPERTY_DEFAULT("format", FORMAT_1);

	ADD_SIGNAL(MethodInfo("changed"));

	BIND_ENUM_CONSTANT(VISIBILITY_MODE_DEFAULT);
	BIND_ENUM_CONSTANT(VISIBILITY_MODE_FORCE_HIDE);
	BIND_ENUM_CONSTANT(VISIBILITY_MODE_FORCE_SHOW);
}

void TileMap::_tile_set_changed() {
	emit_signal(SNAME("changed"));
	_recreate_internals();
}

TileMap::TileMap() {
	set_notify_transform(true);
	set_notify_local_transform(false);

	layers.resize(1);
}

TileMap::~TileMap() {
	if (tile_set.is_valid()) {
		tile_set->disconnect("changed", callable_mp(this, &TileMap::_tile_set_changed));
	}
	_clear_internals();
}
