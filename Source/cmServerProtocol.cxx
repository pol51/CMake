/*============================================================================
  CMake - Cross Platform Makefile Generator
  Copyright 2015 Stephen Kelly <steveire@gmail.com>

  Distributed under the OSI-approved BSD License (the "License");
  see accompanying file Copyright.txt for details.

  This software is distributed WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the License for more information.
============================================================================*/

#include "cmServerProtocol.h"

#include "cmServer.h"
#include "cmVersionMacros.h"
#include "cmGlobalGenerator.h"
#include "cmLocalGenerator.h"

#if defined(CMAKE_BUILD_WITH_CMAKE)
# include "cm_jsoncpp_value.h"
# include "cm_jsoncpp_reader.h"
#endif

cmServerProtocol::cmServerProtocol(cmMetadataServer* server, std::string buildDir)
  : Server(server), CMakeInstance(0), m_buildDir(buildDir)
{

}

cmServerProtocol::~cmServerProtocol()
{
  delete this->CMakeInstance;
}

void cmServerProtocol::processRequest(const std::string& json)
{
  Json::Reader reader;
  Json::Value value;
  reader.parse(json, value);

  if (this->Server->GetState() == cmMetadataServer::Started)
    {
    if (value["type"] == "handshake")
      {
      this->ProcessHandshake(value["protocolVersion"].asString());
      }
    }
  if (this->Server->GetState() == cmMetadataServer::ProcessingRequests)
    {
    if (value["type"] == "version")
      {
      this->ProcessVersion();
      }
    }
}

void cmServerProtocol::ProcessHandshake(std::string const& protocolVersion)
{
  // TODO: Handle version.
  (void)protocolVersion;

  this->Server->SetState(cmMetadataServer::Initializing);
  this->CMakeInstance = new cmake;
  std::set<std::string> emptySet;
  if(!this->CMakeInstance->GetState()->LoadCache(m_buildDir.c_str(),
                                                 true, emptySet, emptySet))
    {
    // Error;
    return;
    }

  const char* genName =
      this->CMakeInstance->GetState()
          ->GetInitializedCacheValue("CMAKE_GENERATOR");
  if (!genName)
    {
    // Error
    return;
    }

  const char* sourceDir =
      this->CMakeInstance->GetState()
          ->GetInitializedCacheValue("CMAKE_HOME_DIRECTORY");
  if (!sourceDir)
    {
    // Error
    return;
    }

  this->CMakeInstance->SetHomeDirectory(sourceDir);
  this->CMakeInstance->SetHomeOutputDirectory(m_buildDir);
  this->CMakeInstance->SetGlobalGenerator(
    this->CMakeInstance->CreateGlobalGenerator(genName));

  this->CMakeInstance->LoadCache();
  this->CMakeInstance->SetSuppressDevWarnings(true);
  this->CMakeInstance->SetWarnUninitialized(false);
  this->CMakeInstance->SetWarnUnused(false);
  this->CMakeInstance->PreLoadCMakeFiles();

  Json::Value obj = Json::objectValue;
  obj["progress"] = "initialized";

  this->Server->WriteResponse(obj);

  // First not? But some other mode that aborts after ActualConfigure
  // and creates snapshots?
  this->CMakeInstance->Configure();

  obj["progress"] = "configured";

  this->Server->WriteResponse(obj);

  if (!this->CMakeInstance->GetGlobalGenerator()->Compute())
    {
    // Error
    return;
    }

  obj["progress"] = "computed";

  this->Server->WriteResponse(obj);

  auto srcDir = this->CMakeInstance->GetState()->GetSourceDirectory();

  Json::Value idleObj = Json::objectValue;
  idleObj["progress"] = "idle";
  idleObj["source_dir"] = srcDir;
  idleObj["binary_dir"] = this->CMakeInstance->GetState()->GetBinaryDirectory();
  idleObj["project_name"] = this->CMakeInstance->GetGlobalGenerator()
      ->GetLocalGenerators()[0]->GetProjectName();

  this->Server->SetState(cmMetadataServer::ProcessingRequests);

  this->Server->WriteResponse(idleObj);
}

void cmServerProtocol::ProcessVersion()
{
  Json::Value obj = Json::objectValue;
  obj["version"] = CMake_VERSION;

  this->Server->WriteResponse(obj);
}
