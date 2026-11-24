/*
 Copyright (c) 2026 XRSpades contributors

 This file is part of ZeroSpades, a fork of OpenSpades.
 Distributed under the GNU General Public License, version 3 or later.
 See <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <random>

class asIScriptEngine;

namespace spades {
	/** The same math/geometry ABI in the client and the isolated skin recorder. */
	// A supplied generator keeps preview randomness deterministic and independent
	// of gameplay's thread-local RNG. The caller owns it for the engine's lifetime.
	void RegisterMathScriptBindings(asIScriptEngine* engine, std::mt19937* random = nullptr);
} // namespace spades
