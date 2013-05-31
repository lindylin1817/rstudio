/*
 * SessionEnvironment.cpp
 *
 * Copyright (C) 2009-12 by RStudio, Inc.
 *
 * Unless you have received this program directly from RStudio pursuant
 * to the terms of a commercial license agreement with RStudio, then
 * this program is licensed to you under the terms of version 3 of the
 * GNU Affero General Public License. This program is distributed WITHOUT
 * ANY EXPRESS OR IMPLIED WARRANTY, INCLUDING THOSE OF NON-INFRINGEMENT,
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE. Please refer to the
 * AGPL (http://www.gnu.org/licenses/agpl-3.0.txt) for more details.
 *
 */

#include "SessionEnvironment.hpp"
#include "EnvironmentMonitor.hpp"

#include <algorithm>

#include <core/Exec.hpp>

#define INTERNAL_R_FUNCTIONS
#include <r/RSexp.hpp>
#include <r/RExec.hpp>
#include <r/session/RSession.hpp>
#include <r/RInterface.hpp>
#include <session/SessionModuleContext.hpp>

#include "EnvironmentUtils.hpp"

#define TOP_FUNCTION 1

using namespace core ;

namespace session {
namespace modules { 
namespace environment {

EnvironmentMonitor s_environmentMonitor;

namespace {

// return the function context at the given depth
RCNTXT* getFunctionContext(const int depth, int* pFoundDepth = NULL)
{
   RCNTXT* pRContext = r::getGlobalContext();
   int currentDepth = 0;
   while (pRContext->callflag)
   {
      if (pRContext->callflag & CTXT_FUNCTION)
      {
         if (++currentDepth == depth)
         {
            break;
         }
      }
      pRContext = pRContext->nextcontext;
   }
   if (pFoundDepth)
   {
      *pFoundDepth = currentDepth;
   }
   return pRContext;
}

SEXP getEnvironment(const int depth)
{
   return depth == 0 ? R_GlobalEnv : getFunctionContext(depth)->cloenv;
}

std::string functionNameFromContext(RCNTXT* pContext)
{
   return r::sexp::asString(PRINTNAME(CAR(pContext->call)));
}

std::string getFunctionName(int depth)
{
   return functionNameFromContext(getFunctionContext(depth));
}

/* TODO(jmcphers) - extract source refs
void getSourceRefFromContext(const RCNTXT* pContext,
                             std::string* pFileName,
                             int* pLineNumber)
{
   r::sexp::Protect rProtect;
   *pLineNumber = r::sexp::asInteger(CAR(pContext->srcref));
   SEXP filename = r::sexp::getAttrib(
            pContext->srcref,
            "srcfile");
   *pFileName = r::sexp::asString(filename);
   return;
}
*/

json::Array callFramesAsJson()
{
   RCNTXT* pRContext = r::getGlobalContext();
   json::Array listFrames;
   int contextDepth = 0;

   while (pRContext->callflag)
   {
      if (pRContext->callflag & CTXT_FUNCTION)
      {
         json::Object varFrame;
         varFrame["context_depth"] = ++contextDepth;
         varFrame["function_name"] = functionNameFromContext(pRContext);

         /* TODO(jmcphers) - extract source refs
         std::string filename;
         int lineNumber = 0;
         getSourceRefFromContext(pRContext, &filename, &lineNumber);
         varFrame["file_name"] = filename;
         varFrame["line_number"] = lineNumber;
         */

         listFrames.push_back(varFrame);
      }
      pRContext = pRContext->nextcontext;
   }
   return listFrames;
}

json::Array environmentListAsJson(int depth)
{
    using namespace r::sexp;
    Protect rProtect;
    std::vector<Variable> vars;
    listEnvironment(getEnvironment(depth), false, &rProtect, &vars);

    // get object details and transform to json
    json::Array listJson;
    std::transform(vars.begin(),
                   vars.end(),
                   std::back_inserter(listJson),
                   varToJson);
    return listJson;
}

Error listEnvironment(boost::shared_ptr<int> pContextDepth,
                      const json::JsonRpcRequest&,
                      json::JsonRpcResponse* pResponse)
{
   // return list
   pResponse->setResult(environmentListAsJson(*pContextDepth));
   return Success();
}

void enqueContextDepthChangedEvent(int depth)
{
   json::Object varJson;

   // emit an event to the client indicating the new call frame and the
   // current state of the environment
   varJson["context_depth"] = depth;
   varJson["environment_list"] = environmentListAsJson(depth);
   varJson["call_frames"] = callFramesAsJson();
   varJson["function_name"] = getFunctionName(depth);

   ClientEvent event (client_events::kContextDepthChanged, varJson);
   module_context::enqueClientEvent(event);
}

Error setContextDepth(boost::shared_ptr<int> pContextDepth,
                      const json::JsonRpcRequest& request,
                      json::JsonRpcResponse* pResponse)
{
   // get the requested depth
   int requestedDepth;
   Error error = json::readParam(request.params, 0, &requestedDepth);
   if (error)
      return error;

   // set state for the new depth
   *pContextDepth = requestedDepth;

   // populate the new state on the client
   enqueContextDepthChangedEvent(*pContextDepth);

   return Success();
}


void onDetectChanges(module_context::ChangeSource source)
{
   s_environmentMonitor.checkForChanges();
}

void onConsolePrompt(boost::shared_ptr<int> pContextDepth)
{
   int depth = 0;
   RCNTXT* pContextTop = getFunctionContext(TOP_FUNCTION, &depth);

   // we entered (or left) a call frame
   if (pContextTop->cloenv != s_environmentMonitor.getMonitoredEnvironment())
   {
      // start monitoring the enviroment at the new depth
      s_environmentMonitor.setMonitoredEnvironment(pContextTop->cloenv);
      *pContextDepth = depth;
      enqueContextDepthChangedEvent(depth);
   }
}


} // anonymous namespace

json::Value environmentStateAsJson()
{
   json::Object stateJson;
   int contextDepth;
   RCNTXT* pContext = getFunctionContext(TOP_FUNCTION, &contextDepth);
   stateJson["context_depth"] = contextDepth;
   stateJson["function_name"] = functionNameFromContext(pContext);
   stateJson["call_frames"] = callFramesAsJson();
   return stateJson;
}

Error initialize()
{
   boost::shared_ptr<int> pContextDepth = boost::make_shared<int>(0);

   // begin monitoring the top-level environment
   RCNTXT* pContext = getFunctionContext(TOP_FUNCTION, pContextDepth.get());
   s_environmentMonitor.setMonitoredEnvironment(pContext->cloenv);

   // subscribe to events
   using boost::bind;
   using namespace session::module_context;
   events().onDetectChanges.connect(bind(onDetectChanges, _1));
   events().onConsolePrompt.connect(bind(onConsolePrompt, pContextDepth));

   json::JsonRpcFunction listEnv =
         boost::bind(listEnvironment, pContextDepth, _1, _2);
   json::JsonRpcFunction setCtxDepth =
         boost::bind(setContextDepth, pContextDepth, _1, _2);

   // source R functions
   ExecBlock initBlock ;
   initBlock.addFunctions()
      (bind(registerRpcMethod, "list_environment", listEnv))
      (bind(registerRpcMethod, "set_context_depth", setCtxDepth))
      (bind(sourceModuleRFile, "SessionEnvironment.R"));

   return initBlock.execute();
}
   
} // namespace environment
} // namespace modules
} // namesapce session

