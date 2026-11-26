/*
 Copyright (c) 2026 XRSpades contributors

 This file is part of ZeroSpades, a fork of OpenSpades.

 OpenSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 OpenSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with OpenSpades.  If not, see <http://www.gnu.org/licenses/>.

 */

#pragma once

#include <memory>

#include <Core/Math.h>

namespace spades {
	namespace client {
		class Client;
		struct ImpactMarksImpl;

		/**
		 * Bullet and spade holes in blocks (cg_impactMarks). A block hit is drawn
		 * as a small voxel model of itself with real pits cut into whichever of
		 * its faces were hit, its edges, corners and a solid core always left, the map's
		 * own faces of it left out; a renderer that cannot leave them out gets a
		 * flat chipped mark on the face instead. Gone when the block is, or
		 * changes (rebuilt in another colour), and after a while; only the newest
		 * few hundred are kept. Purely what this client draws: nothing reaches
		 * the server, and the block itself is untouched.
		 */
		class ImpactMarks {
			std::unique_ptr<ImpactMarksImpl> impl;

		public:
			explicit ImpactMarks(Client&);
			~ImpactMarks();

			void Clear();
			/** A hit at `hitPos` on `block`'s face whose outward normal is
			 * `normal`. `spade`: a bigger, rougher mark than a bullet's. */
			void Add(const Vector3& hitPos, const IntVector3& block, const IntVector3& normal,
			         bool spade);
			void Update(float dt);
			/** Cut into the block itself, where the renderer can leave its faces
			 * out; false where it cannot. */
			bool AddCarved(const Vector3& hitPos, const IntVector3& block, const IntVector3& normal,
			               bool spade);
			void Draw();
		};
	} // namespace client
} // namespace spades
