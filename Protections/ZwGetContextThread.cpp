#include "stdafx.h"
#include "..\OllyExtHook.h"
#include "..\OllyExtAssembler.h"
#include "..\OllyExtRemote.h"
#include "ZwGetContextThread.h"


#define ASSEMBLER_ERROR_HANDLER() \
	delete [] localHookProc; \
	hookFree( process, LIBNAME, PROCNAME, g_hook ); \
	return;

#define LIBNAME ( L"ntdll.dll" )
#define PROCNAME ( "ZwGetContextThread" )
#define HOOK_SIZE ( 0x100 )


static bool g_applied = false;
static sHook g_hook = { 0 };


void* zwGetContextThreadOrig( void )
{
	if( g_hook.trampolineProc )
		return g_hook.trampolineProc;
	
	if( process )
		return remoteGetProcAddress( process, LIBNAME, PROCNAME );

	return NULL;
}


void zwGetContextThreadReset( void )
{
	g_applied = false;
}


void zwGetContextThreadApply( bool protect )
{
	if( protect )
	{
		if( g_applied ) return;

		wchar_t procNameW[TEXTLEN];
		Asciitounicode( PROCNAME, strlen( PROCNAME ), procNameW, TEXTLEN );

		g_hook.remoteTargetProc = NULL;
		g_hook.trampolineProcSize = 0;
		g_hook.trampolineProc = NULL;
		g_hook.hookSize = HOOK_SIZE;
		g_hook.remoteHookProc = NULL;

		bool allocResult = hookAlloc( process, LIBNAME, PROCNAME, g_hook );
		PLUGIN_CHECK_ERROR( allocResult, return;, UNABLE_TO_ALLOC_HOOK, LIBNAME, procNameW );

		BYTE* localHookProc = new BYTE[HOOK_SIZE];
		PLUGIN_CHECK_ERROR( localHookProc,
			hookFree( process, LIBNAME, PROCNAME, g_hook ); \
			return;,
			UNABLE_TO_ALLOCATE_LOCAL_MEMORY );

		wchar_t cmd[64] = { 0 };
		DWORD codeBase = (DWORD)g_hook.remoteHookProc;
		DWORD localEip = 0;
		DWORD patchEip = 0;
		DWORD labelEip = 0;

		// Assembler init
		ASSEMBLER_FLUSH();
		ASSEMBLER_SET_CODEBASE( codeBase );
		ASSEMBLER_SET_TARGET_BUFFER( localHookProc );

		// Call original function
		ASSEMBLE_AND_STEP( L"mov eax, esp", localEip );
		ASSEMBLE_AND_STEP( L"mov ecx, 0x02", localEip );
		ASSEMBLER_SET_LABEL( "loopLabel", localEip );
		ASSEMBLE_AND_STEP( L"mov edx, [ecx*4+eax]", localEip );
		ASSEMBLE_AND_STEP( L"push edx", localEip );
		ASSEMBLE_AND_STEP( L"dec ecx", localEip );
		ASSEMBLE_AND_STEP( L"cmp ecx, 0x00", localEip );
		labelEip = ASSEMBLER_GET_LABEL( "loopLabel" );
		wsprintf( cmd, L"jnz 0x%08X", codeBase + labelEip );
		ASSEMBLE_AND_STEP( cmd, localEip );
		wsprintf( cmd, L"call 0x%08X", g_hook.trampolineProc );
		ASSEMBLE_AND_STEP( cmd, localEip );

		// Fake DRX registers
		ASSEMBLE_AND_STEP( L"mov edx, [esp + 0x08]", localEip );
		ASSEMBLE_AND_STEP( L"mov dword ptr[edx + 0x04], 0x00", localEip );
		ASSEMBLE_AND_STEP( L"mov dword ptr[edx + 0x08], 0x00", localEip );
		ASSEMBLE_AND_STEP( L"mov dword ptr[edx + 0x0C], 0x00", localEip );
		ASSEMBLE_AND_STEP( L"mov dword ptr[edx + 0x10], 0x00", localEip );
		ASSEMBLE_AND_STEP( L"mov dword ptr[edx + 0x14], 0xffff0ff0", localEip );
		ASSEMBLE_AND_STEP( L"mov dword ptr[edx + 0x18], 0x00000100", localEip );

		// Return and clean the stack
		ASSEMBLE_AND_STEP( L"ret 0x08", localEip );

		ASSEMBLER_FLUSH();

		bool attachResult = hookAttach( process, LIBNAME, PROCNAME, localHookProc, g_hook );
		PLUGIN_CHECK_ERROR( attachResult,
			delete [] localHookProc; \
			hookFree( process, LIBNAME, PROCNAME, g_hook ); \
			return;,
			UNABLE_TO_ATTACH_HOOK, LIBNAME, procNameW );

		delete [] localHookProc;

		g_applied = true;
	}
	else
	{
		if( !g_applied ) return;

		wchar_t procNameW[TEXTLEN];
		Asciitounicode( PROCNAME, strlen( PROCNAME ), procNameW, TEXTLEN );

		bool freeResult = hookFree( process, LIBNAME, PROCNAME, g_hook );
		PLUGIN_CHECK_ERROR( freeResult, return;, UNABLE_TO_FREE_HOOK, LIBNAME, procNameW );

		g_applied = false;
	}
}
