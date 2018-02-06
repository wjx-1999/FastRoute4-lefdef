/* Copyright 2014-2018 Rsyn
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* 
 * File:   PhysicalRoutingGrid.h
 * Author: jucemar
 *
 * Created on February 2, 2018, 9:06 PM
 */

namespace Rsyn {

inline Rsyn::PhysicalLayer PhysicalRoutingGrid::getLayer() const {
	return data->clsLayer;
} // end method 

// -----------------------------------------------------------------------------

inline const std::vector<Rsyn::PhysicalTracks> & PhysicalRoutingGrid::allTracks() const {
	return data->clsTracks;
} // end method 

// -----------------------------------------------------------------------------

inline const Bounds & PhysicalRoutingGrid::getBounds() const {
	return data->clsBounds;
} // end method 

// -----------------------------------------------------------------------------

inline DBUxy PhysicalRoutingGrid::getPosition() const {
	return data->clsBounds[LOWER];
} // end method 

// -----------------------------------------------------------------------------

inline DBU PhysicalRoutingGrid::getPosition(const Dimension dim) const {
	return data->clsBounds[LOWER][dim];
} // end method 

// -----------------------------------------------------------------------------

inline DBUxy PhysicalRoutingGrid::getSpacing() const {
	return data->clsSpacing;
} // end method 

// -----------------------------------------------------------------------------

inline DBU PhysicalRoutingGrid::getSpacing(const Dimension dim) const {
	return data->clsSpacing[dim];
} // end method 

// -----------------------------------------------------------------------------

inline int PhysicalRoutingGrid::getNumTracks(const Dimension dim) const {
	return data->clsNumTracks[dim];
} // end method 

// -----------------------------------------------------------------------------

inline int PhysicalRoutingGrid::getNumRows() const {
	return data->clsNumTracks[Y];
} // end method 

// -----------------------------------------------------------------------------

inline int PhysicalRoutingGrid::getNumCols() const {
	return data->clsNumTracks[X];
} // end method 

// -----------------------------------------------------------------------------

inline int PhysicalRoutingGrid::getNumTracks() const {
	return data->clsNumTracks[X] + data->clsNumTracks[Y];
} // end method 

// -----------------------------------------------------------------------------

inline int PhysicalRoutingGrid::getRow(const DBU posY, const bool clamp) const {
	const DBU pos = posY - getPosition(Y);
	const int id = static_cast<int>(pos / getSpacing(Y));
	return clamp? std::max(0, std::min(id, getNumRows() -1)) : id;
} // end method 

// -----------------------------------------------------------------------------

inline int PhysicalRoutingGrid::getCol(const DBU posX, const bool clamp) const {
	const DBU pos = posX - getPosition(X);
	const int id = static_cast<int>(pos / getSpacing(X));
	return clamp? std::max(0, std::min(id, getNumCols() - 1)) : id;
} // end method 

// -----------------------------------------------------------------------------

inline DBUxy PhysicalRoutingGrid::getPosition(const int col, const int row) const {
	DBU MAX_DBU = std::numeric_limits<DBU>::max();
	DBUxy pos(MAX_DBU, MAX_DBU);
	if (row < 0 || row >= getNumRows() || col < 0 || col >= getNumCols())
		return pos;
	pos[X] = getColPosition(col);
	pos[Y] = getRowPosition(row);
	return pos;
} // end method 

// -----------------------------------------------------------------------------

inline DBU PhysicalRoutingGrid::getRowPosition(const int row) const {
	if (row < 0 || row > getNumRows())
		return std::numeric_limits<DBU>::max();
	return getPosition(Y) + (getSpacing(Y) * row);
} // end method 

// -----------------------------------------------------------------------------

inline DBU PhysicalRoutingGrid::getRowMaxPosition() const {
	return getRowPosition(getNumRows());
} // end method 

// -----------------------------------------------------------------------------

inline DBU PhysicalRoutingGrid::getColPosition(const int col) const {
	if (col < 0 || col > getNumCols())
		return std::numeric_limits<DBU>::max();
	return getPosition(X) + (getSpacing(X) * col);
} // end method 

// -----------------------------------------------------------------------------

inline DBU PhysicalRoutingGrid::getColMaxPosition() const {
	return getColPosition(getNumCols());
} // end method 

// -----------------------------------------------------------------------------

inline DBUxy PhysicalRoutingGrid::getTrackMaxPosition() const {
	DBUxy pos;
	pos[X] = getColMaxPosition();
	pos[Y] = getRowMaxPosition();
	return pos;
} // end method 

// -----------------------------------------------------------------------------

inline DBU PhysicalRoutingGrid::getTrackMaxPosition(const Dimension dim) const {
	DBU pos = std::numeric_limits<DBU>::max();
	if (dim == X)
		pos = getColMaxPosition();
	if (dim == Y)
		pos = getRowMaxPosition();
	return pos;
} // end method 

// -----------------------------------------------------------------------------

inline Rsyn::PhysicalRoutingGrid PhysicalRoutingGrid::getBottomRoutingGrid() const {
	return data->clsBottomRoutingGrid;
} // end method 

// -----------------------------------------------------------------------------

inline Rsyn::PhysicalRoutingGrid PhysicalRoutingGrid::getTopRoutingGrid() const {
	return data->clsTopRoutingGrid;
} // end method 

// -----------------------------------------------------------------------------

inline bool PhysicalRoutingGrid::hasBottomRoutingGrid() const {
	return data->clsBottomRoutingGrid != nullptr;
} // end method 

// -----------------------------------------------------------------------------

inline bool PhysicalRoutingGrid::hasTopRoutingGrid() const {
	return data->clsTopRoutingGrid != nullptr;
} // end method 

// -----------------------------------------------------------------------------

inline DBUxy PhysicalRoutingGrid::getGridMinPosition() const {
	return data->clsBounds[LOWER];
} // end method

// -----------------------------------------------------------------------------

inline DBUxy PhysicalRoutingGrid::getGridMaxPosition() const {
	return data->clsBounds[UPPER];
} // end method

// -----------------------------------------------------------------------------

inline DBUxy PhysicalRoutingGrid::getTrackMinPosition(const PhysicalLayerDirection dir, const int index) const {
	DBU x = 0;
	DBU y = 0;
	if (dir == Rsyn::VERTICAL) {
		x = getColPosition(index);
		y = getGridMinPosition().y;
	} else {
		x = getGridMinPosition().x;
		y = getRowPosition(index);
	} // end else
	return DBUxy(x, y);
} // end method

// -----------------------------------------------------------------------------

inline DBUxy PhysicalRoutingGrid::getTrackMaxPosition(const PhysicalLayerDirection dir, const int index) const {
	DBU x = 0;
	DBU y = 0;
	if (dir == Rsyn::VERTICAL) {
		x = getColPosition(index);
		y = getGridMaxPosition().y;
	} else {
		x = getGridMaxPosition().x;
		y = getRowPosition(index);
	} // end else
	return DBUxy(x, y);
} // end method

// -----------------------------------------------------------------------------

} // end namespace 