//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Source code:           https://github.com/OpenHoldem/openholdembot/
//    Forums:                http://www.maxinmontreal.com/forums/index.php
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose:
//
//******************************************************************************
 
#include "CParseTreeTerminalNodeUserVariable.h"
#include <math.h>
///#include "CAutoplayerTrace.h"
#include "CFormula.h"
#include "CFunctionCollection.h"
#include "CParserSymbolTable.h"
#include "TokenizerConstants.h"
#include "..\Debug_DLL\debug.h"
#include "..\Formula_DLL\CAutoplayerTrace.h"
#include "..\Formula_DLL\CFormula.h"
#include "..\Numerical_Functions_DLL\NumericalFunctions.h"
#include "..\Preferences_DLL\Preferences.h"
#include "..\StringFunctions_DLL\string_functions.h"
#include "..\UserVariables_DLL\CMemorySymbolsCollection.h"
#include "..\UserVariables_DLL\COpenPPLUserVariablesCollection.h"
#include "..\WindowFunctions_DLL\window_functions.h"

CParseTreeTerminalNodeUserVariable::CParseTreeTerminalNodeUserVariable(
  int relative_line_number, CString name) : 
  CParseTreeTerminalNodeIdentifier(relative_line_number, 
    name) {
  _node_type = kTokenActionUserVariableToBeSet;
}

CParseTreeTerminalNodeUserVariable::~CParseTreeTerminalNodeUserVariable() {
}

double CParseTreeTerminalNodeUserVariable::Evaluate(bool log /* = false */){
  CString name = _terminal_name;
  write_log(Preferences()->debug_formula(), 
    "[CParseTreeTerminalNode] Evaluating node type %i %s\n", 
		_node_type, name);
  Formula()->AutoplayerTrace()->SetLastEvaluatedRelativeLineNumber(_relative_line_number);
  if (name.Left(4).MakeLower() == "user") {
    OpenPPLUserVariablesCollection()->Set(name);
    return true;
  }
  // Covering both me_st_ and me_inc_ here.
  // me_re_ is a normal identifier, as it
  // doesn't require a continuation at the next condition.
  // Also me_add_ amd me_sub_ since openHoldem 11.1.0.
  if (name.Left(3) == "me_") {
    double temp_result;
    MemorySymbolsCollection()->EvaluateSymbol(name,
      &temp_result, true);
    return temp_result;
  }
  assert(kThisMustNotHappen);
  return false;
}

CString CParseTreeTerminalNodeUserVariable::EvaluateToString(bool log /* = false */) {
  double numerical_result = Evaluate(log);
  CString result;
  if (IsInteger(numerical_result) && EvaluatesToBinaryNumber()) {
    // Generqate binary representation;
    result.Format("%s", IntToBinaryString(int(numerical_result)));
  } else {
    // Generate floating-point representation
    // with 3 digits precision
    result.Format("%.3f", numerical_result);
  }
  return result;
}

CString CParseTreeTerminalNodeUserVariable::Serialize() {
  if (_node_type == kTokenIdentifier) {
    return _terminal_name;
  } else {
    // Unhandled note-type, probably new and therefore not yet handled
   write_log(k_always_log_errors, "[CParseTreeTerminalNode] ERROR: Unhandled node-type %i in serialization of parse-tree\n",
      _node_type);
    return "";
  }
}

