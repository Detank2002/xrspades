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
		 * A chipped hole where a bullet or the spade hit a block (cg_impactMarks):
		 * a small voxel model on the block's face, a dark pit in a ragged rim of
		 * the block's own colour standing proud of it, so the light catches the
		 * chips. One per hit. Gone when the block is, or changes (rebuilt in
		 * another colour), and faded away after a while; only the newest few
		 * hundred are kept. Purely what this client draws: nothing reaches the
		 * server, and the block itself is untouched.
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
			/** Cut into the face itself, where the renderer can leave it out. */
			bool AddCarved(const Vector3& hitPos, const IntVector3& block, const IntVector3& normal,
			               bool spade);
			void Draw();
		};
	} // namespace client
} // namespace spades
