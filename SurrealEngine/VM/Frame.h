#pragma once

#include "ExpressionValue.h"
#include <optional>
#include "Iterator.h"

class DebuggerWindow;
class Bytecode;
class UObject;
class UFunction;
class Expression;
struct ExpressionEvalResult;

enum class FrameRunState
{
	Running,
	DebugBreak,
	StepInto,
	StepOver,
	StepOut
};

enum class LatentRunState
{
	Continue,
	Stop,
	Sleep,
	FinishAnim,
	FinishInterpolation,
	MoveTo,
	MoveToward,
	StrafeTo,
	StrafeFacing,
	TurnTo,
	TurnToward,
	WaitForLanding
};

struct Breakpoint
{
	NameString Class;
	NameString Function;
	NameString State;
	Expression* Expr = nullptr;
	UProperty* Property = nullptr; // For watchpoints. Not implemented yet.
	bool Enabled = true;
};

class LocalVariables
{
public:
	LocalVariables(UStruct* func);
	~LocalVariables();

	LocalVariables(const LocalVariables&) = delete;
	LocalVariables& operator=(const LocalVariables&) = delete;

	UStruct* Func = nullptr;
	void* Data = nullptr;

private:
	// The variables of most functions fit in this buffer, so a script call does not allocate memory for them
	enum { InlineSize = 256, InlineAlignment = 16 };
	alignas(16) uint8_t InlineData[InlineSize];
};

// The arguments of one function call. A call has few arguments, so the list keeps them in the object itself
// and a call does not allocate memory for them.
class CallArgList
{
public:
	CallArgList() = default;
	CallArgList(const CallArgList&) = delete;
	CallArgList& operator=(const CallArgList&) = delete;

	~CallArgList()
	{
		for (size_t i = 0; i < Count; i++)
			Items[i].~ExpressionValue();
		if (Items != InlineItems())
			::operator delete(Items);
	}

	void push_back(ExpressionValue&& value)
	{
		if (Count == Capacity)
			Grow();
		new(&Items[Count++]) ExpressionValue(std::move(value));
	}

	ExpressionValue* data() { return Items; }
	size_t size() const { return Count; }
	ExpressionValue& operator[](size_t index) { return Items[index]; }
	ExpressionValue& back() { return Items[Count - 1]; }
	ExpressionValue* begin() { return Items; }
	ExpressionValue* end() { return Items + Count; }

private:
	enum { InlineCapacity = 12 };

	ExpressionValue* InlineItems() { return reinterpret_cast<ExpressionValue*>(InlineStorage); }

	void Grow()
	{
		size_t newCapacity = Capacity * 2;
		ExpressionValue* newItems = static_cast<ExpressionValue*>(::operator new(newCapacity * sizeof(ExpressionValue)));
		for (size_t i = 0; i < Count; i++)
		{
			new(&newItems[i]) ExpressionValue(std::move(Items[i]));
			Items[i].~ExpressionValue();
		}
		if (Items != InlineItems())
			::operator delete(Items);
		Items = newItems;
		Capacity = newCapacity;
	}

	alignas(ExpressionValue) uint8_t InlineStorage[InlineCapacity * sizeof(ExpressionValue)];
	ExpressionValue* Items = InlineItems();
	size_t Capacity = InlineCapacity;
	size_t Count = 0;
};

class Frame
{
public:
	static ExpressionValue Call(UFunction* func, UObject* instance, Array<ExpressionValue> args);
	static ExpressionValue Call(UFunction* func, UObject* instance, ArrayView<ExpressionValue> args);
	static std::string GetCallstack();
	static std::string GetDisassembly(Expression* statement);

	static bool AddBreakpoint(const NameString& cls, const NameString& func, const NameString& state = {}, int statementIndex = 0);

	static std::function<void()> RunDebugger;
	static Array<Breakpoint> Breakpoints;
	static Array<Frame*> Callstack;
	static FrameRunState RunState;
	static Frame* StepFrame;
	static Expression* StepExpression;
	static std::string ExceptionText;

	static void Break();
	static void Resume();
	static void StepInto();
	static void StepOver();
	static void StepOut();
	static void ThrowException(const std::string& text);

	static std::unique_ptr<Iterator> CreatedIterator;

	// Counters for the performance report of the test harness
	static uint64_t ScriptCallCount;
	static uint64_t NativeCallCount;
	static uint64_t StatementCount;

	Frame(UObject* instance, UStruct* func);

	// A frame that is only an entry in the call stack. It has no local variables. Native function calls use it.
	struct NoVariables { };
	Frame(UObject* instance, UStruct* func, NoVariables);

	void SetState(UStruct* func);

	void GotoLabel(const NameString& label);
	void Tick();

	std::string GetName();

	LatentRunState LatentState = LatentRunState::Continue;

	LocalVariables* Variables = nullptr; // null for a frame of a native function call
	UObject* Object = nullptr;
	UStruct* Func = nullptr;
	size_t StatementIndex = 0;
	Array<std::unique_ptr<Iterator>> Iterators;

private:
	ExpressionEvalResult Run();
	void ProcessSwitch(const ExpressionValue& condition);

	static ExpressionEvalResult RunExpr(Expression* expr, UObject* self, UObject* context, void* localVariables);

	static ExpressionValue Call(UFunction* func, UObject* instance, CallArgList& args);
	static ExpressionValue CallNative(UFunction* func, UObject* instance, CallArgList& args);
	static ExpressionValue CallScript(UFunction* func, UObject* instance, CallArgList& args);
	static void TraceCall(UFunction* func, UObject* instance, CallArgList& args);

	// The variables are a part of the frame, so a script call does not allocate memory for them. "Variables" points to them.
	std::optional<LocalVariables> VariableStorage;

	struct ActiveCallStackFrame
	{
		ActiveCallStackFrame(Frame* frame) { Frame::Callstack.push_back(frame); }
		~ActiveCallStackFrame() { Frame::Callstack.pop_back(); }
	};
};
