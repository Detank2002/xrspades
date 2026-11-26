/*
 Copyright (c) 2026 XRSpades contributors

 This file is part of ZeroSpades, a fork of OpenSpades.

 ZeroSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 ZeroSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with ZeroSpades.  If not, see <http://www.gnu.org/licenses/>.

 */

// A map chunk's vertex (VulkanMapChunk::VertexPosition / VertexSurface), 8
// bytes where it was 20: what the rest was is worked out here from the face it
// is a corner of. A tiled GPU reads every vertex once to bin it and again for
// each tile it draws in, so the less it reads, the cheaper each triangle; the
// position comes in a stream of its own (r_mapVertexStreams), all binning
// needs.
//
// location 0: x, y, z in the chunk (0..16); the face's direction (0..5),
// which of its corners this is (0..3, bit 0 along u, bit 1 along v) << 3, and
// 0x20 for a face 2 blocks a side (a far chunk's, VulkanMapChunk::BuildCoarse)
// location 1: the colour, and the face's ambient-occlusion tile (0..255)
layout(location = 0) in uvec4 mapPositionAttribute;
layout(location = 1) in uvec4 mapSurfaceAttribute;

// The six directions in the order VulkanMapChunk::Update emits them, with
// the edges each face's corners step along (EmitVertex's u and v)
const vec3 kMapFaceNormal[6] = vec3[6](vec3(0.0, 0.0, 1.0), vec3(0.0, 0.0, -1.0),
                                       vec3(-1.0, 0.0, 0.0), vec3(1.0, 0.0, 0.0),
                                       vec3(0.0, -1.0, 0.0), vec3(0.0, 1.0, 0.0));
const vec3 kMapFaceU[6] = vec3[6](vec3(-1.0, 0.0, 0.0), vec3(1.0, 0.0, 0.0),
                                  vec3(0.0, 0.0, 1.0), vec3(0.0, 0.0, 1.0),
                                  vec3(0.0, 0.0, 1.0), vec3(0.0, 0.0, 1.0));
const vec3 kMapFaceV[6] = vec3[6](vec3(0.0, 1.0, 0.0), vec3(0.0, 1.0, 0.0),
                                  vec3(0.0, -1.0, 0.0), vec3(0.0, 1.0, 0.0),
                                  vec3(1.0, 0.0, 0.0), vec3(-1.0, 0.0, 0.0));
// The sun "shading" byte the chunk builder baked per face (GL parity: 0 for
// +Z, +Y and +-X, 220 for -Z, 255 for -Y)
const float kMapFaceShading[6] = float[6](0.0, 220.0, 0.0, 0.0, 255.0, 0.0);

struct MapVertex {
	vec3 position;      // in the chunk
	vec3 normal;
	float shading;      // 0..255, as the old colour.w
	vec3 fixedPosition; // the face's centre * 2, in the chunk (as the old attribute)
	uvec2 aoCoord;      // into the 256x256 AO atlas (as the old attribute)
	uvec3 color;        // 0..255
};

vec3 DecodeMapPosition() { return vec3(mapPositionAttribute.xyz); }

MapVertex DecodeMapVertex() {
	MapVertex v;
	const uint face = min(mapPositionAttribute.w & 7u, 5u);
	const uint corner = (mapPositionAttribute.w >> 3u) & 3u;
	const float cu = float(corner & 1u), cv = float(corner >> 1u);
	const float size = (mapPositionAttribute.w & 0x20u) != 0u ? 2.0 : 1.0;
	v.position = vec3(mapPositionAttribute.xyz);
	v.normal = kMapFaceNormal[face];
	v.shading = kMapFaceShading[face];
	// The first corner's position * 2 + u + v (EmitVertex's sx, sy, sz)
	v.fixedPosition = v.position * 2.0 - (cu * 2.0 - 1.0) * size * kMapFaceU[face] -
	                  (cv * 2.0 - 1.0) * size * kMapFaceV[face];
	const uint ao = mapSurfaceAttribute.w;
	v.aoCoord = uvec2((ao & 15u) * 16u + (corner & 1u) * 15u, (ao >> 4u) * 16u + (corner >> 1u) * 15u);
	v.color = mapSurfaceAttribute.xyz;
	return v;
}
