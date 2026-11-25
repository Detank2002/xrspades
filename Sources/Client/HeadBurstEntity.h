/*
 Copyright (c) 2013 yvt

 This file is part of OpenSpades.

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

#include "ILocalEntity.h"
#include "IModel.h"
#include <Core/Math.h>

#include <vector>

namespace spades {
	namespace client {
		class Client;
		class GameMap;
		class IRenderer;
		class IAudioChunk;

		/** Comic gore effect: a skull bursting into voxel chunks when a
		 *  player is killed by a headshot. A single entity handles all
		 *  the chunks of one burst. */
		class HeadBurstEntity : public ILocalEntity {
			Client* client;
			IRenderer& renderer;

			struct Chunk {
				Matrix4 matrix;  // current position and tumbling orientation
				Vector3 velocity;
				Vector3 angVel;  // tumble speed, radians per second
				float scale;
				float life;     // seconds left before this chunk disappears
				int modelIndex;
				bool splatDone; // blood already drawn on the first impact
				bool resting = false; // lying still: no longer moved
			};
			std::vector<Chunk> chunks;

			// burst origin, used for fog and sound distance culling
			Vector3 origin;

			// chunk models, picked randomly per chunk; missing files are
			// skipped so the effect degrades gracefully without assets
			std::vector<Handle<IModel>> models;
			// How many chunks this burst counts against the cap on every
			// burst's chunks at once (kMaxLiveChunks), until it goes.
			int counted = 0;

		public:
			HeadBurstEntity(Client& client, Vector3 origin, Vector3 shotDir);
			~HeadBurstEntity() override;

			bool Update(float dt) override;
			void Render3D() override;

		private:
			/** Blood splat where a chunk lands: flying particles plus
			 *  persistent marks on the blocks (Client::Bleed is private,
			 *  so the same particle look is reproduced here). */
			void BloodSplat(GameMap& map, Vector3 pos, Vector3 vel);
		};
	} // namespace client
} // namespace spades
