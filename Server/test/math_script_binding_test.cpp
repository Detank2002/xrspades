// Regression: the InvertedFast property must be bound with the Matrix4 return
// type AngelScript sees. A float-declared property calling a Matrix4-returning
// C++ method mismatched the sret ABI and corrupted the caller's stack.

#include <cstdio>
#include <random>

#include <ScriptBindings/ScriptManager.h>
#include <AngelScript/addons/scriptstdstring.h>

#include <Core/Math.h>
#include <ScriptBindings/MathScript.h>

using namespace spades;

namespace {
	int failures = 0;
	void Message(const asSMessageInfo* msg, void*) {
		std::printf("%s (%d,%d): %s\n", msg->section, msg->row, msg->col, msg->message);
	}
	void Check(bool ok, const char* what) {
		if (!ok) {
			std::printf("FAIL: %s\n", what);
			++failures;
		}
	}
} // namespace

int main() {
	std::mt19937 random(1234);
	asIScriptEngine* engine = asCreateScriptEngine();
	if (!engine) {
		std::printf("FAIL: cannot create script engine\n");
		return 1;
	}
	engine->SetMessageCallback(asFUNCTION(Message), nullptr, asCALL_CDECL);
	RegisterStdString(engine);
	RegisterMathScriptBindings(engine, &random);
	engine->SetDefaultNamespace("spades");

	// InversedFast of a translation is the exact inverse translation. The
	// property returns a Matrix4 by value: a float-declared binding used to
	// mismatch the sret ABI here and corrupt the caller's stack.
	const char* source =
	  "spades::Matrix4 Test() {\n"
	  "    spades::Matrix4 m = spades::CreateTranslateMatrix(spades::Vector3(2, -3, 4));\n"
	  "    spades::Matrix4 inv = m.InvertedFast;\n"
	  "    return inv;\n"
	  "}\n";

	asIScriptModule* module = engine->GetModule("MathBinding", asGM_ALWAYS_CREATE);
	int r = module->AddScriptSection("test", source);
	Check(r >= 0, "script section added");
	r = module->Build();
	Check(r >= 0, "script built");
	if (r < 0) {
		engine->ShutDownAndRelease();
		return failures != 0;
	}

	asIScriptFunction* fn = module->GetFunctionByName("Test");
	Check(fn != nullptr, "Test function found");
	if (fn) {
		asIScriptContext* ctx = engine->CreateContext();
		r = ctx->Prepare(fn);
		Check(r >= 0, "context prepared");
		if (r >= 0) {
			r = ctx->Execute();
			Check(r == asEXECUTION_FINISHED, "script executed without crashing");
		}
		ctx->Release();
	}

	engine->ShutDownAndRelease();
	if (failures == 0)
		std::printf("math script binding regression checks passed\n");
	return failures != 0;
}
