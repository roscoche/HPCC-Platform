/*##############################################################################

    HPCC SYSTEMS software Copyright (C) 2012 HPCC Systems®.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
############################################################################## */

#pragma warning (disable : 4786)
// TpWrapper.cpp: implementation of the CTpWrapper class.
//
//////////////////////////////////////////////////////////////////////

#include "TpWrapper.hpp"
#include <stdio.h>
#include "workunit.hpp"
#include "exception_util.hpp"

const char* MSG_FAILED_GET_ENVIRONMENT_INFO = "Failed to get environment information.";

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

IPropertyTree* CTpWrapper::getEnvironment(const char* xpath)
{
    Owned<IEnvironmentFactory> envFactory = getEnvironmentFactory(true);
    Owned<IConstEnvironment> constEnv = envFactory->openEnvironment();
    Owned<IPropertyTree> root = &constEnv->getPTree();
    if (!xpath || !*xpath)
        return LINK(root);
    IPropertyTree* pSubTree = root->queryPropTree( xpath );
    if (pSubTree)
        return LINK(pSubTree);

    return NULL;
}

bool CTpWrapper::getClusterLCR(const char* clusterType, const char* clusterName)
{
    bool bLCR = false;
    if (!clusterType || !*clusterType || !clusterName || !*clusterName)
        return bLCR;

    Owned<IEnvironmentFactory> envFactory = getEnvironmentFactory(true);
    Owned<IConstEnvironment> constEnv = envFactory->openEnvironment();
    Owned<IPropertyTree> root = &constEnv->getPTree();

    StringBuffer xpath;
    xpath.appendf("Software/%s[@name='%s']", clusterType, clusterName);
    IPropertyTree* pCluster = root->queryPropTree( xpath.str() );
    if (!pCluster)
        throw MakeStringException(ECLWATCH_CLUSTER_NOT_IN_ENV_INFO, "'%s %s' is not defined.", clusterType, clusterName);

    bLCR = !pCluster->getPropBool("@Legacy");

    return bLCR;
}

void CTpWrapper::getClusterMachineList(double clientVersion,
                                       const char* ClusterType,
                                       const char* ClusterPath,
                                       const char* ClusterDirectory,
                                       IArrayOf<IEspTpMachine> &MachineList,
                                       bool& hasThorSpareProcess,
                                       const char* ClusterName)
{
    try
    {
        StringBuffer returnStr,path;
        getAttPath(ClusterPath,path);
        set<string> machineNames; //used for checking duplicates

        if (strcmp(eqTHORMACHINES,ClusterType) == 0)
        {
            bool multiSlaves = false;
            getMachineList(eqThorMasterProcess,path.str(),"", ClusterDirectory, MachineList);
            getThorSlaveMachineList(clientVersion, ClusterName, ClusterDirectory, MachineList);
            unsigned count = MachineList.length();
            getThorSpareMachineList(clientVersion, ClusterName, ClusterDirectory, MachineList);

            //The checkMultiSlavesFlag is for legacy multiSlaves environment, not for new environments.
            //count < MachineList.length(): There is some node for eqThorSpareProcess being added to the MachineList.
            if (!checkMultiSlavesFlag(ClusterName) &&(count < MachineList.length()))
                hasThorSpareProcess = true;
        }
        else if (strcmp(eqHOLEMACHINES,ClusterType) == 0)
        {
            getMachineList(eqHoleSocketProcess,path.str(),"", ClusterDirectory, MachineList);
            getMachineList(eqHoleProcessorProcess,path.str(),"", ClusterDirectory, MachineList);
            getMachineList(eqHoleControlProcess,path.str(),"", ClusterDirectory, MachineList);
            getMachineList(eqHoleCollatorProcess,path.str(),"", ClusterDirectory, MachineList);
            getMachineList(eqHoleStandbyProcess,path.str(),"", ClusterDirectory, MachineList);
        }
        else if (strcmp(eqROXIEMACHINES,ClusterType) == 0)
        {
            getMachineList("RoxieServerProcess",path.str(),"", ClusterDirectory, MachineList, &machineNames);
        }
        else if (strcmp(eqMACHINES,ClusterType) == 0)
        {
            //load a list of available machines.......
            getMachineList("Computer","/Environment/Hardware","", ClusterDirectory, MachineList);
        }
        else if (strcmp("AVAILABLEMACHINES",ClusterType) == 0)
        {
            getMachineList("Computer","/Environment/Hardware",eqMachineAvailablability, ClusterDirectory, MachineList);
        }
        else if (strcmp("DROPZONE",ClusterType) == 0)
        {
            getDropZoneMachineList(clientVersion, false, MachineList);
        }
        else if (strcmp("STANDBYNNODE",ClusterType) == 0)
        {
            getThorSpareMachineList(clientVersion, ClusterName, ClusterDirectory, MachineList);
            getMachineList(eqHoleStandbyProcess,path.str(),"", ClusterDirectory, MachineList);
        }
        else if (strcmp("THORSPARENODES",ClusterType) == 0)
        {
            getThorSpareMachineList(clientVersion, ClusterName, ClusterDirectory, MachineList);
        }
        else if (strcmp("HOLESTANDBYNODES",ClusterType) == 0)
      {
            getMachineList(eqHoleStandbyProcess,path.str(),"", ClusterDirectory, MachineList);
      }
    }
    catch(IException* e){   
      StringBuffer msg;
      e->errorMessage(msg);
        WARNLOG("%s", msg.str());
        e->Release();
    }
    catch(...){
        WARNLOG("Unknown Exception caught within CTpWrapper::getClusterMachineList");
    }
}


void CTpWrapper::fetchInstances(const char* ServiceType, IPropertyTree& service, 
                                IArrayOf<IEspTpMachine>& tpMachines)
{
    Owned<IPropertyTreeIterator> instances = service.getElements("Instance");
    if (instances->first()) {
        do {
            IPropertyTree& instanceNode = instances->query();

            IEspTpMachine* machine = createTpMachine("", "");
            getMachineInfo(*machine, instanceNode, "/Environment/Software", ServiceType, "@computer");
         machine->setPort( instanceNode.getPropInt("@port") );
            const char* directory = instanceNode.queryProp("@directory");
            if (directory && *directory)
                machine->setDirectory( directory );

         tpMachines.append(*machine);

        } while (instances->next());
    }
}

void CTpWrapper::getTpDaliServers(double clientVersion, IArrayOf<IConstTpDali>& list)
{
    Owned<IPropertyTree> root = getEnvironment("Software");
    if (!root)
        throw MakeStringExceptionDirect(ECLWATCH_CANNOT_GET_ENV_INFO, MSG_FAILED_GET_ENVIRONMENT_INFO);

    Owned<IPropertyTreeIterator> services= root->getElements(eqDali);
    ForEach(*services)
    {
        IPropertyTree& serviceTree = services->query();

        Owned<IEspTpDali> pService = createTpDali("","");
        const char* name = serviceTree.queryProp("@name");
        pService->setName(name);
        pService->setDescription(serviceTree.queryProp("@description"));
        pService->setBackupComputer(serviceTree.queryProp("@backupCoputer"));
        pService->setBackupDirectory(serviceTree.queryProp("@backupDirectory"));
        pService->setBuild(serviceTree.queryProp("@build"));
        pService->setType(eqDali);

        StringBuffer tmpDir, tmpAuditDir;
        if (getConfigurationDirectory(root->queryPropTree("Directories"), "log", "dali", name, tmpDir))
        {
            const char* pStr = tmpDir.str();
            if (pStr)
            {
                if (strchr(pStr, '/'))
                    tmpDir.append("/");
                else
                    tmpDir.append("\\");
                tmpAuditDir.set(tmpDir.str());

                tmpDir.append("server");
                pService->setLogDirectory(tmpDir.str());
                if (clientVersion >= 1.27)
                {
                    tmpAuditDir.append("audit");
                    pService->setAuditLogDirectory(tmpAuditDir.str());
                }
            }
        }
        else
        {
            pService->setLogDirectory(serviceTree.queryProp("@LogDir")); // backward compatible
        }

        IArrayOf<IEspTpMachine> tpMachines;
        fetchInstances(eqDali, serviceTree, tpMachines);
        pService->setTpMachines(tpMachines);

        list.append(*pService.getLink());
    }
}

void CTpWrapper::getTpEclServers(IArrayOf<IConstTpEclServer>& list, const char* serverName)
{
    Owned<IPropertyTree> root = getEnvironment("Software");
    if (!root)
        throw MakeStringExceptionDirect(ECLWATCH_CANNOT_GET_ENV_INFO, MSG_FAILED_GET_ENVIRONMENT_INFO);

    Owned<IPropertyTreeIterator> services= root->getElements(eqEclServer);
    ForEach(*services)
    {
        IPropertyTree& serviceTree = services->query();

        Owned<IEspTpEclServer> pService = createTpEclServer("","");
        const char* name = serviceTree.queryProp("@name");
        if (serverName && stricmp(name, serverName))
            continue;

        pService->setName(name);
        pService->setDescription(serviceTree.queryProp("@description"));
        pService->setBuild(serviceTree.queryProp("@build"));

        StringBuffer tmpDir;
        if (getConfigurationDirectory(root->queryPropTree("Directories"), "log", "eclserver", name, tmpDir))
        {
            pService->setLogDirectory( tmpDir.str() );
        }
        else
        {
            pService->setLogDirectory(serviceTree.queryProp("@eclLogDir"));
        }
        pService->setType(eqEclServer);

        IArrayOf<IEspTpMachine> tpMachines;
        fetchInstances(eqEclServer, serviceTree, tpMachines);
        pService->setTpMachines(tpMachines);

        list.append(*pService.getLink());
    }
}

void CTpWrapper::getTpEclCCServers(IArrayOf<IConstTpEclServer>& list, const char* serverName)
{
    Owned<IPropertyTree> root = getEnvironment("Software");
    if (!root)
        throw MakeStringExceptionDirect(ECLWATCH_CANNOT_GET_ENV_INFO, MSG_FAILED_GET_ENVIRONMENT_INFO);

    getTpEclCCServers(root, list, serverName);
    return;
}

void CTpWrapper::getTpEclCCServers(IPropertyTree* environmentSoftware, IArrayOf<IConstTpEclServer>& list, const char* serverName)
{
    if (!environmentSoftware)
        return;

    Owned<IPropertyTreeIterator> services= environmentSoftware->getElements(eqEclCCServer);
    ForEach(*services)
    {
        IPropertyTree& serviceTree = services->query();

        const char* name = serviceTree.queryProp("@name");
        const char* logDir = serviceTree.queryProp("@logDir");
        if (serverName && stricmp(name, serverName))
            continue;

        Owned<IEspTpEclServer> pService = createTpEclServer("","");
        pService->setName(name);
        pService->setDescription(serviceTree.queryProp("@description"));
        pService->setBuild(serviceTree.queryProp("@build"));

        StringBuffer tmpDir;
        if (getConfigurationDirectory(environmentSoftware->queryPropTree("Directories"), "log", "eclccserver", name, tmpDir))
        {
            pService->setLogDirectory( tmpDir.str() );
        }
        else
        {
            pService->setLogDirectory(logDir);
        }
        pService->setType(eqEclCCServer);

        IArrayOf<IEspTpMachine> tpMachines;
        fetchInstances(eqEclCCServer, serviceTree, tpMachines);
        pService->setTpMachines(tpMachines);

        list.append(*pService.getLink());
    }
}

void CTpWrapper::getTpEclAgents(IArrayOf<IConstTpEclAgent>& list, const char* agentName)
{
    Owned<IPropertyTree> root = getEnvironment("Software");
    if (!root)
        throw MakeStringExceptionDirect(ECLWATCH_CANNOT_GET_ENV_INFO, MSG_FAILED_GET_ENVIRONMENT_INFO);

    Owned<IPropertyTreeIterator> services= root->getElements(eqEclAgent);
    ForEach(*services)
    {
        IPropertyTree& serviceTree = services->query();

        const char* name = serviceTree.queryProp("@name");
        if (agentName && stricmp(name, agentName))
            continue;

        const char* daliServers = serviceTree.queryProp("@daliServers");
        const char* logDir = serviceTree.queryProp("@logDir");
        Owned<IEspTpEclAgent> pService = createTpEclAgent("","");
        pService->setDaliServer(daliServers);

        StringBuffer tmpDir;
        if (getConfigurationDirectory(root->queryPropTree("Directories"), "log", "eclagent", name, tmpDir))
        {
            pService->setLogDir( tmpDir.str() );
        }
        else
        {
            pService->setLogDir(logDir);
        }

        pService->setName(name);
        pService->setDescription(serviceTree.queryProp("@description"));
        pService->setBuild(serviceTree.queryProp("@build"));
        pService->setType(eqEclAgent);

        IArrayOf<IEspTpMachine> tpMachines;
      fetchInstances(eqEclAgent, serviceTree, tpMachines);
      pService->setTpMachines(tpMachines);

        list.append(*pService.getLink());
    }
}

void CTpWrapper::getTpEclSchedulers(IArrayOf<IConstTpEclScheduler>& list, const char* serverName)
{
    Owned<IPropertyTree> root = getEnvironment("Software");
    if (!root)
        throw MakeStringExceptionDirect(ECLWATCH_CANNOT_GET_ENV_INFO, MSG_FAILED_GET_ENVIRONMENT_INFO);

    Owned<IPropertyTreeIterator> services= root->getElements(eqEclScheduler);
    ForEach(*services)
    {
        IPropertyTree& serviceTree = services->query();

        const char* name = serviceTree.queryProp("@name");
        const char* logDir = serviceTree.queryProp("@logDir");
        if (serverName && stricmp(name, serverName))
            continue;

        Owned<IEspTpEclScheduler> pService = createTpEclScheduler("","");
        pService->setName(name);
        pService->setDescription(serviceTree.queryProp("@description"));
        pService->setBuild(serviceTree.queryProp("@build"));

        StringBuffer tmpDir;
        if (getConfigurationDirectory(root->queryPropTree("Directories"), "log", "eclscheduler", name, tmpDir))
        {
            pService->setLogDirectory( tmpDir.str() );
        }
        else
        {
            pService->setLogDirectory(logDir);
        }
        pService->setType(eqEclScheduler);

        IArrayOf<IEspTpMachine> tpMachines;
      fetchInstances(eqEclScheduler, serviceTree, tpMachines);
      pService->setTpMachines(tpMachines);

        list.append(*pService.getLink());
    }
}

void CTpWrapper::getTpEspServers(IArrayOf<IConstTpEspServer>& list)
{
    Owned<IPropertyTree> root = getEnvironment("Software");
    if (!root)
        throw MakeStringExceptionDirect(ECLWATCH_CANNOT_GET_ENV_INFO, MSG_FAILED_GET_ENVIRONMENT_INFO);

    Owned<IPropertyTreeIterator> services= root->getElements(eqEsp);
    ForEach(*services)
    {
        IPropertyTree& serviceTree = services->query();
        Owned<IEspTpEspServer> pService = createTpEspServer("","");
        const char* name = serviceTree.queryProp("@name");
        pService->setName(name);
        pService->setDescription(serviceTree.queryProp("@description"));
        pService->setBuild(serviceTree.queryProp("@build"));
        pService->setType(eqEsp);

        StringBuffer tmpDir;
        if (getConfigurationDirectory(root->queryPropTree("Directories"), "log", "esp", name, tmpDir))
        {
            pService->setLogDirectory( tmpDir.str() );
        }
        else
        {
            const char* logDir = serviceTree.queryProp("@logDir");
            if (logDir && *logDir)
                pService->setLogDirectory(logDir);
        }

        IArrayOf<IEspTpMachine> tpMachines;
        fetchInstances(eqEsp, serviceTree, tpMachines);
        pService->setTpMachines(tpMachines);

        Owned<IPropertyTreeIterator> iBinding = serviceTree.getElements("EspBinding");
        IArrayOf<IEspTpBinding> tpBindings;
        ForEach(*iBinding)
        {
            IPropertyTree& binding = iBinding->query();
            const char* service = binding.queryProp("@service");
            if (service && *service)
            {
                Owned<IEspTpBinding> pTpBinding = createTpBinding("", "");
                pTpBinding->setName       (binding.queryProp("@name"));
                pTpBinding->setService(service);
                pTpBinding->setProtocol   (binding.queryProp("@protocol"));
                pTpBinding->setPort       (binding.queryProp("@port"));

                StringBuffer xpath;
                xpath.appendf("EspService[@name='%s']", service);
                IPropertyTree* pServiceNode = root->queryPropTree(xpath.str());
                if (pServiceNode)
                {
                    const char* serviceType = pServiceNode->queryProp("Properties/@type");
                    if (serviceType && *serviceType)
                        pTpBinding->setServiceType(serviceType);
                    const char* bindingType = pServiceNode->queryProp("Properties/@bindingType");
                    if (bindingType && *bindingType)
                        pTpBinding->setBindingType(bindingType);
                    const char* buildSet = pServiceNode->queryProp("@buildSet");
                    if (buildSet && *buildSet)
                        pTpBinding->setServiceBuildSet(buildSet);
                }
                tpBindings.append(*pTpBinding.getLink());
            }
        }

        pService->setTpBindings( tpBindings);
        list.append(*pService.getLink());
    }
}

void CTpWrapper::getTpDfuServers(IArrayOf<IConstTpDfuServer>& list)
{
    Owned<IPropertyTree> root = getEnvironment("Software");
    if (!root)
        throw MakeStringExceptionDirect(ECLWATCH_CANNOT_GET_ENV_INFO, MSG_FAILED_GET_ENVIRONMENT_INFO);

    Owned<IPropertyTreeIterator> services= root->getElements(eqDfu);
    ForEach(*services)
    {
        IPropertyTree& serviceTree = services->query();

        Owned<IEspTpDfuServer> pService = createTpDfuServer("","");
        const char* name = serviceTree.queryProp("@name");
        pService->setName(name);
        pService->setDescription(serviceTree.queryProp("@description"));
        pService->setBuild(serviceTree.queryProp("@build"));
        pService->setQueue(serviceTree.queryProp("@queue"));
        pService->setType(eqDfu);

        StringBuffer tmpDir;
        if (getConfigurationDirectory(root->queryPropTree("Directories"), "log", "dfuserver", name, tmpDir))
        {
            pService->setLogDirectory( tmpDir.str() );
        }
        else
        {
            pService->setLogDirectory(serviceTree.queryProp("@dfuLogDir"));
        }

        IArrayOf<IEspTpMachine> tpMachines;
        fetchInstances(eqDfu, serviceTree, tpMachines);
        pService->setTpMachines(tpMachines);

        list.append(*pService.getLink());
    }
}


void CTpWrapper::getTpSashaServers(IArrayOf<IConstTpSashaServer>& list)
{
    Owned<IPropertyTree> root = getEnvironment("Software");
    if (!root)
        throw MakeStringExceptionDirect(ECLWATCH_CANNOT_GET_ENV_INFO, MSG_FAILED_GET_ENVIRONMENT_INFO);

    Owned<IPropertyTreeIterator> services= root->getElements(eqSashaServer);
    ForEach(*services)
    {
        IPropertyTree& serviceTree = services->query();

        Owned<IEspTpSashaServer> pService = createTpSashaServer("","");
        const char* name = serviceTree.queryProp("@name");
        pService->setName(name);
        pService->setDescription(serviceTree.queryProp("@description"));
        pService->setBuild(serviceTree.queryProp("@build"));

        StringBuffer tmpDir;
        if (getConfigurationDirectory(root->queryPropTree("Directories"), "log", "sasha", name, tmpDir))
        {
            pService->setLogDirectory( tmpDir.str() );
        }
        else
        {
            pService->setLogDirectory(serviceTree.queryProp("@logDir"));
        }

        IArrayOf<IEspTpMachine> tpMachines;
        fetchInstances(eqSashaServer, serviceTree, tpMachines);
        pService->setTpMachines(tpMachines);

        list.append(*pService.getLink());
    }
}

void CTpWrapper::getTpLdapServers(IArrayOf<IConstTpLdapServer>& list)
{
    Owned<IPropertyTree> root = getEnvironment("Software");
    if (!root)
        throw MakeStringExceptionDirect(ECLWATCH_CANNOT_GET_ENV_INFO, MSG_FAILED_GET_ENVIRONMENT_INFO);

    Owned<IPropertyTreeIterator> services= root->getElements(eqLdapServer);
    ForEach(*services)
    {
        IPropertyTree& serviceTree = services->query();

        Owned<IEspTpLdapServer> pService = createTpLdapServer("","");
        pService->setName(serviceTree.queryProp("@name"));
        pService->setDescription(serviceTree.queryProp("@description"));
        pService->setBuild(serviceTree.queryProp("@build"));

        IArrayOf<IEspTpMachine> tpMachines;
        fetchInstances(eqLdapServer, serviceTree, tpMachines);

        int port = serviceTree.getPropInt("@ldapPort", 0);

        if (tpMachines.length() == 0)
        {
           const char* computer = serviceTree.queryProp("@computer");
           if (computer && *computer)
           {
               Owned<IEspTpMachine> machine = createTpMachine("", "");

               setMachineInfo(computer, "LDAPServerProcess", *machine);
               StringBuffer tmpPath;
               StringBuffer ppath("/Environment/Software");
               setAttPath(ppath, "Instance", "name", computer, tmpPath);
               machine->setPath(tmpPath.str());

               if (port)
                 machine->setPort( port );
               tpMachines.append(*machine.getLink());
           }
        }
        else
        {
           const int nMachines = tpMachines.length();
           for (int i=0; i<nMachines; i++)
              tpMachines.item(i).setPort(port);
        }
        pService->setTpMachines(tpMachines);

        list.append(*pService.getLink());
    }
}

void CTpWrapper::getTpFTSlaves(IArrayOf<IConstTpFTSlave>& list)
{
    Owned<IPropertyTree> root = getEnvironment("Software");
    if (!root)
        throw MakeStringExceptionDirect(ECLWATCH_CANNOT_GET_ENV_INFO, MSG_FAILED_GET_ENVIRONMENT_INFO);

    Owned<IPropertyTreeIterator> services= root->getElements(eqFTSlave);
    ForEach(*services)
    {
        IPropertyTree& serviceTree = services->query();

        Owned<IEspTpFTSlave> pService = createTpFTSlave("","");
        pService->setName(serviceTree.queryProp("@name"));
        pService->setDescription(serviceTree.queryProp("@description"));
        pService->setBuild(serviceTree.queryProp("@build"));

        IArrayOf<IEspTpMachine> tpMachines;
        fetchInstances(eqFTSlave, serviceTree, tpMachines);
        pService->setTpMachines(tpMachines);

        list.append(*pService.getLink());
    }
}

void CTpWrapper::getTpDkcSlaves(IArrayOf<IConstTpDkcSlave>& list)
{
    Owned<IPropertyTree> root = getEnvironment("Software");
    if (!root)
        throw MakeStringExceptionDirect(ECLWATCH_CANNOT_GET_ENV_INFO, MSG_FAILED_GET_ENVIRONMENT_INFO);

    Owned<IPropertyTreeIterator> services= root->getElements(eqDkcSlave);
    ForEach(*services)
    {
        IPropertyTree& serviceTree = services->query();

        Owned<IEspTpDkcSlave> pService =createTpDkcSlave("","");
        pService->setName(serviceTree.queryProp("@name"));
        pService->setDescription(serviceTree.queryProp("@description"));
        pService->setBuild(serviceTree.queryProp("@build"));

        IArrayOf<IEspTpMachine> tpMachines;
        fetchInstances(eqDkcSlave, serviceTree, tpMachines);
        pService->setTpMachines(tpMachines);

        list.append(*pService.getLink());
    }
}

void CTpWrapper::getTpGenesisServers(IArrayOf<IConstTpGenesisServer>& list)
{
    Owned<IPropertyTree> root = getEnvironment("Software");
    if (!root)
        throw MakeStringExceptionDirect(ECLWATCH_CANNOT_GET_ENV_INFO, MSG_FAILED_GET_ENVIRONMENT_INFO);

    Owned<IPropertyTreeIterator> services= root->getElements(eqGenesisServer);
    ForEach(*services)
    {
        IPropertyTree& serviceTree = services->query();

        Owned<IEspTpGenesisServer> pService = createTpGenesisServer("","");
        pService->setName(serviceTree.queryProp("@name"));
        pService->setDescription(serviceTree.queryProp("@description"));
        pService->setBuild(serviceTree.queryProp("@build"));

        IArrayOf<IEspTpMachine> tpMachines;
        fetchInstances(eqGenesisServer, serviceTree, tpMachines);
        pService->setTpMachines(tpMachines);

        list.append(*pService.getLink());
    }
}

void CTpWrapper::getTargetClusterList(IArrayOf<IEspTpLogicalCluster>& clusters, const char* clusterType, const char* clusterName)
{
    Owned<IPropertyTree> root = getEnvironment("Software");
    if (!root)
        throw MakeStringExceptionDirect(ECLWATCH_CANNOT_GET_ENV_INFO, MSG_FAILED_GET_ENVIRONMENT_INFO);

    Owned<IPropertyTreeIterator> clusterIterator = root->getElements("Topology/Cluster");
    if (clusterIterator->first()) 
    {
        do {
            IPropertyTree &cluster0 = clusterIterator->query();
            StringBuffer processName;
            const char* clusterName0 = cluster0.queryProp("@name");
            if (!clusterName0 || !*clusterName0)
                continue;

            bool bAdd = false;
            if (!clusterType || !*clusterType)
            {
                bAdd = true;
            }
            else
            {
                Owned<IPropertyTreeIterator> clusters0= cluster0.getElements(clusterType);
                if (clusters0->first()) 
                {
                    if (!clusterName || !*clusterName)
                    {
                        IPropertyTree &cluster = clusters0->query();                    
                        const char* name = cluster.queryProp("@process");
                        if (name && *name)
                            processName.append(name);

                        bAdd = true;
                    }
                    else
                    {
                        do {
                            IPropertyTree &cluster = clusters0->query();                    
                            const char* name = cluster.queryProp("@process");
                            if (!name||!*name)
                                continue;

                            if (!stricmp(name, clusterName))
                            {
                                bAdd = true;
                                break;
                            }
                        } while (clusters0->next());
                    }
                }
            }

            if (!bAdd)
                continue;

            IEspTpLogicalCluster* pService = createTpLogicalCluster("","");
            pService->setName(clusterName0);
            if (processName.length() > 0)
                pService->setProcess(processName);
            pService->setLanguageVersion("3.0.0");
            clusters.append(*pService);

        } while (clusterIterator->next());
    }
}

void CTpWrapper::queryTargetClusterProcess(double version, const char* processName, const char* clusterType, IArrayOf<IConstTpCluster>& clusterList)
{
    Owned<IEnvironmentFactory> envFactory = getEnvironmentFactory(true);
    Owned<IConstEnvironment> constEnv = envFactory->openEnvironment();
    Owned<IPropertyTree> root = &constEnv->getPTree();

    StringBuffer xpath;
    xpath.appendf("Software/%s[@name='%s']", clusterType, processName);
    IPropertyTree* pClusterTree = root->queryPropTree(xpath.str());
    if (!pClusterTree)
        throw MakeStringExceptionDirect(ECLWATCH_CANNOT_GET_ENV_INFO, MSG_FAILED_GET_ENVIRONMENT_INFO);

    const char* queueName = NULL;
    if (processName&&(stricmp(clusterType,eqThorCluster)==0)) 
    {   
        // only for multi-thor
        // only list first thor cluster on queue
        queueName = pClusterTree->queryProp("@queueName");
        if (!queueName||!*queueName)
            queueName = processName;
    }

    IEspTpCluster* clusterInfo = createTpCluster("","");    
    clusterInfo->setName(processName);
    if (queueName && *queueName)
        clusterInfo->setQueueName(queueName);
    else
        clusterInfo->setQueueName(processName);
    clusterInfo->setDesc(processName);
    clusterInfo->setBuild( pClusterTree->queryProp("@build") );

    clusterInfo->setType(clusterType);

    StringBuffer tmpPath;
    StringBuffer path("/Environment/Software");
    setAttPath(path, clusterType, "name", processName, tmpPath);
    clusterInfo->setPath(tmpPath.str());

    StringBuffer dirStr;
    if (!getConfigurationDirectory(root->queryPropTree("Software/Directories"), "run", clusterType, processName, dirStr))
    {
        dirStr.clear().append(pClusterTree->queryProp("@directory"));
    }

    clusterInfo->setDirectory(dirStr.str());

    StringBuffer tmpDir;
    if (getConfigurationDirectory(root->queryPropTree("Software/Directories"), "log", clusterType, processName, tmpDir))
    {
        clusterInfo->setLogDirectory( tmpDir.str() );
    }
    else
    {
        const char* logDir = pClusterTree->queryProp("@logDir");
        if (logDir)
            clusterInfo->setLogDirectory( logDir );
    }

    clusterInfo->setPrefix("");
    if(pClusterTree->hasProp("@dataBuild"))
        clusterInfo->setDataModel(pClusterTree->queryProp("@dataBuild"));

    clusterList.append(*clusterInfo);

    //find out OS
    OS_TYPE os = OS_WINDOWS;
    unsigned int clusterTypeLen = strlen(clusterType);
    const char* childType = NULL;
    const char* clusterType0 = NULL;
    if (clusterTypeLen > 4)
    {
        if (!strnicmp(clusterType, "roxie", 4))
        {
            childType = "RoxieServerProcess[1]";
            clusterType0 = eqROXIEMACHINES;
        }
        else if (!strnicmp(clusterType, "thor", 4))
        {
            childType = "ThorMasterProcess";
            clusterType0 = eqTHORMACHINES;
        }
        else
        {
            childType = "HoleControlProcess";
            clusterType0 = eqHOLEMACHINES;
        }
    }

    if (childType)
    {
        IPropertyTree* pChild = pClusterTree->queryPropTree(childType);
        if (pChild)
        {
            const char* computer = pChild->queryProp("@computer");
            IPropertyTree* pHardware = root->queryPropTree("Hardware");
            if (computer && *computer && pHardware)
            {
                StringBuffer xpath;
                xpath.appendf("Computer[@name='%s']/@computerType", computer);
                const char* computerType = pHardware->queryProp( xpath.str() );
                if (computerType && *computerType)
                {
                    xpath.clear().appendf("ComputerType[@name='%s']/@opSys", computerType);
                    const char* opSys = pHardware->queryProp( xpath.str() );
                    if (!stricmp(opSys, "linux") || !stricmp( opSys, "solaris"))
                        os = OS_LINUX;
                }
            }
        }
    }
    clusterInfo->setOS(os);

    if (clusterType0 && *clusterType0)
    {
        bool hasThorSpareProcess = false;
        IArrayOf<IEspTpMachine> machineList;
        getClusterMachineList(version, clusterType0, tmpPath.str(), dirStr.str(), machineList, hasThorSpareProcess, processName);
        if (machineList.length() > 0)
            clusterInfo->setTpMachines(machineList);
        if (version > 1.14)
            clusterInfo->setHasThorSpareProcess(hasThorSpareProcess);
    }

    return;
}

void CTpWrapper::queryTargetClusters(double version, const char* clusterType, const char* clusterName, IArrayOf<IEspTpTargetCluster>& targetClusterList)
{
    try
    {
        Owned<IEnvironmentFactory> envFactory = getEnvironmentFactory(true);
        Owned<IConstEnvironment> constEnv = envFactory->openEnvironment();
        Owned<IPropertyTree> root = &constEnv->getPTree();

        Owned<IPropertyTreeIterator> clusters= root->getElements("Software/Topology/Cluster");
        if (!clusters->first())
            return;

        do {
            IPropertyTree &cluster = clusters->query();                 
            const char* name = cluster.queryProp("@name");
            if (!name||!*name)
                continue;

            if (clusterName && *clusterName && strcmp(clusterName, name))
                continue;

            const char* prefix = cluster.queryProp("@prefix");

            Owned<IPropertyTreeIterator> thorClusters= cluster.getElements(eqThorCluster);
            Owned<IPropertyTreeIterator> roxieClusters= cluster.getElements(eqRoxieCluster);
            Owned<IPropertyTreeIterator> eclCCServerProcesses= cluster.getElements(eqEclCCServer);
            Owned<IPropertyTreeIterator> eclServerProcesses= cluster.getElements(eqEclServer);
            Owned<IPropertyTreeIterator> eclSchedulerProcesses= cluster.getElements(eqEclScheduler);
            Owned<IPropertyTreeIterator> eclAgentProcesses= cluster.getElements(eqEclAgent);

            if (clusterType && !stricmp(clusterType, eqThorCluster) && !thorClusters->first())
                continue;

            if (clusterType && !stricmp(clusterType, eqRoxieCluster) && !roxieClusters->first())
                continue;

            if (clusterType && !stricmp(clusterType, eqHoleCluster) && (roxieClusters->first() || thorClusters->first()))
                continue;

            IEspTpTargetCluster* clusterInfo = createTpTargetCluster("","");                    
            clusterInfo->setName(name);
            if (prefix && *prefix)
                clusterInfo->setPrefix(prefix);

            //Read Cluster process
            clusterInfo->setType(eqHoleCluster);
            IArrayOf<IConstTpCluster>& clusterList = clusterInfo->getTpClusters();
            if (thorClusters->first())
            {
                clusterInfo->setType(eqThorCluster);
                do {
                    IPropertyTree &thorCluster = thorClusters->query();                 
                    const char* process = thorCluster.queryProp("@process");
                    if (process && *process)
                    {
                        queryTargetClusterProcess(version, process, eqThorCluster, clusterList);
                    }
                } while (thorClusters->next());
            }

            if (roxieClusters->first())
            {
                clusterInfo->setType(eqRoxieCluster);
                do {
                    IPropertyTree &roxieCluster = roxieClusters->query();                   
                    const char* process = roxieCluster.queryProp("@process");
                    if (process && *process)
                    {
                        queryTargetClusterProcess(version, process, eqRoxieCluster, clusterList);
                    }
                } while (roxieClusters->next());
            }

            //Read eclCCServer process
            IArrayOf<IConstTpEclServer>& eclCCServerList = clusterInfo->getTpEclCCServers();
            if (eclCCServerProcesses->first())
            {
                IPropertyTree &eclCCServerProcess = eclCCServerProcesses->query();                  
                const char* process = eclCCServerProcess.queryProp("@process");
                if (process && *process)
                {
                    getTpEclCCServers(eclCCServerList, process);
                }
            }

            //Read eclServer process
            if ((version >= 1.19) && eclServerProcesses->first())
            {
                IArrayOf<IConstTpEclServer>& eclServerList = clusterInfo->getTpEclServers();
                IPropertyTree &eclServerProcess = eclServerProcesses->query();
                const char* process = eclServerProcess.queryProp("@process");
                if (process && *process)
                {
                    getTpEclServers(eclServerList, process);
                }
            }

            //Read eclAgent process
            IArrayOf<IConstTpEclAgent>& eclAgentList = clusterInfo->getTpEclAgents();
            if (eclAgentProcesses->first())
            {
                IPropertyTree &eclAgentProcess = eclAgentProcesses->query();                    
                const char* process = eclAgentProcess.queryProp("@process");
                if (process && *process)
                {
                    getTpEclAgents(eclAgentList, process);
                }
            }

            //Read eclScheduler process
            IArrayOf<IConstTpEclScheduler>& eclSchedulerList = clusterInfo->getTpEclSchedulers();
            if (eclSchedulerProcesses->first())
            {
                IPropertyTree &eclSchedulerProcess = eclSchedulerProcesses->query();                    
                const char* process = eclSchedulerProcess.queryProp("@process");
                if (process && *process)
                {
                    getTpEclSchedulers(eclSchedulerList, process);
                }
            }

            targetClusterList.append(*clusterInfo);
        } while (clusters->next());
    }
    catch(IException* e){   
        StringBuffer msg;
        e->errorMessage(msg);
        WARNLOG("%s", msg.str());
        e->Release();
    }
    catch(...){
        WARNLOG("Unknown Exception caught within CTpWrapper::getClusterList");
    }
}

void CTpWrapper::getClusterProcessList(const char* ClusterType, IArrayOf<IEspTpCluster>& clusterList, bool ignoreduplicatqueues, bool ignoreduplicategroups)
{
    try
    {
        Owned<IEnvironmentFactory> envFactory = getEnvironmentFactory(true);
        Owned<IConstEnvironment> constEnv = envFactory->openEnvironment();
        Owned<IPropertyTree> root = &constEnv->getPTree();
        IPropertyTree* pSoftware = root->queryPropTree("Software");
        if (!pSoftware)
            throw MakeStringExceptionDirect(ECLWATCH_CANNOT_GET_ENV_INFO, MSG_FAILED_GET_ENVIRONMENT_INFO);

        StringArray queuesdone;
        StringArray groupsdone;
        Owned<IPropertyTreeIterator> clusters= pSoftware->getElements(ClusterType);
        if (clusters->first()) {
            do {
                IPropertyTree &cluster = clusters->query();
                const char* name = cluster.queryProp("@name");
                if (!name||!*name)
                    continue;
                const char* queueName = NULL;
                const char* groupName = NULL;
                if (name&&(stricmp(ClusterType,eqThorCluster)==0))
                {
                    // only for multi-thor
                    // only list first thor cluster on queue
                    queueName = cluster.queryProp("@queueName");
                    if (!queueName||!*queueName)
                        queueName = name;
                    if (ignoreduplicatqueues)
                    {
                        bool done=false;
                        ForEachItemIn(i,queuesdone)
                        {
                            if (strcmp(queuesdone.item(i),queueName)==0)
                            {
                                done = true;
                                break;
                            }
                        }
                        if (done)
                            continue;
                        queuesdone.append(queueName);
                    }
                    groupName = cluster.queryProp("@nodeGroup");
                    if (!groupName||!*groupName)
                        groupName = name;
                    if (ignoreduplicategroups)
                    {
                        bool done=false;
                        ForEachItemIn(i,groupsdone)
                        {
                            if (strcmp(groupsdone.item(i),groupName)==0)
                            {
                                done = true;
                                break;
                            }
                        }
                        if (done)
                            continue;
                        groupsdone.append(groupName);
                    }

                }

                IEspTpCluster* clusterInfo = createTpCluster("","");
                clusterInfo->setName(name);
                if (queueName && *queueName)
                    clusterInfo->setQueueName(queueName);
                else
                    clusterInfo->setQueueName(name);
                clusterInfo->setDesc(name);
                clusterInfo->setBuild( cluster.queryProp("@build") );

                StringBuffer path("/Environment/Software");
                StringBuffer tmpPath;
                setAttPath(path, ClusterType, "name", name, tmpPath);

                clusterInfo->setType(ClusterType);

                StringBuffer tmpDir;
                if (getConfigurationDirectory(root->queryPropTree("Software/Directories"), "run", ClusterType, name, tmpDir))
                {
                    clusterInfo->setDirectory(tmpDir.str());
                }
                else
                {
                    clusterInfo->setDirectory(cluster.queryProp("@directory"));
                }

                tmpDir.clear();
                if (getConfigurationDirectory(root->queryPropTree("Software/Directories"), "log", ClusterType, name, tmpDir))
                {
                    clusterInfo->setLogDirectory( tmpDir.str() );
                }
                else
                {
                    const char* logDir = cluster.queryProp("@logDir");
                    if (logDir)
                        clusterInfo->setLogDirectory( logDir );
                }

                clusterInfo->setPath(tmpPath.str());
                clusterInfo->setPrefix("");
                if(cluster.hasProp("@dataBuild"))
                    clusterInfo->setDataModel(cluster.queryProp("@dataBuild"));

                clusterList.append(*clusterInfo);

                //find out OS
                OS_TYPE os = OS_WINDOWS;
                unsigned int clusterTypeLen = strlen(ClusterType);
                const char* childType = NULL;
                if (clusterTypeLen > 4)
                {
                    if (!strnicmp(ClusterType, "roxie", 4))
                        childType = "RoxieServerProcess[1]";
                    else if (!strnicmp(ClusterType, "thor", 4))
                        childType = "ThorMasterProcess";
                    else
                        childType = "HoleControlProcess";
                }
                if (childType)
                {
                    IPropertyTree* pChild = cluster.queryPropTree(childType);
                    if (pChild)
                    {
                        const char* computer = pChild->queryProp("@computer");
                        IPropertyTree* pHardware = root->queryPropTree("Hardware");
                        if (computer && *computer && pHardware)
                        {
                            StringBuffer xpath;
                            xpath.appendf("Computer[@name='%s']/@computerType", computer);
                            const char* computerType = pHardware->queryProp( xpath.str() );
                            if (computerType && *computerType)
                            {
                                xpath.clear().appendf("ComputerType[@name='%s']/@opSys", computerType);
                                const char* opSys = pHardware->queryProp( xpath.str() );
                                if (!stricmp(opSys, "linux") || !stricmp( opSys, "solaris"))
                                    os = OS_LINUX;
                            }
                        }
                    }
                }
                clusterInfo->setOS(os);

            } while (clusters->next());
        }
    }
    catch(IException* e){   
        StringBuffer msg;
        e->errorMessage(msg);
        WARNLOG("%s", msg.str());
        e->Release();
    }
    catch(...){
        WARNLOG("Unknown Exception caught within CTpWrapper::getClusterList");
    }
}

void CTpWrapper::getHthorClusterList(IArrayOf<IEspTpCluster>& clusterList)
{
    try
    {
        Owned<IEnvironmentFactory> envFactory = getEnvironmentFactory(true);
        Owned<IConstEnvironment> constEnv = envFactory->openEnvironment();
        Owned<IPropertyTree> root = &constEnv->getPTree();
        IPropertyTree* pSoftware = root->queryPropTree("Software");

        const char * ClusterType = "EclAgentProcess";
        Owned<IPropertyTreeIterator> clusters(pSoftware->getElements(ClusterType));
        ForEach(*clusters)
        {
            IPropertyTree &cluster = clusters->query();
            const char* name = cluster.queryProp("@name");
            if (!name||!*name)
                continue;
            unsigned ins = 0;
            Owned<IPropertyTreeIterator> insts = clusters->query().getElements("Instance");
            ForEach(*insts)
            {
                const char *na = insts->query().queryProp("@netAddress");
                if (na&&*na)
                {
                    SocketEndpoint ep(na);
                    if (!ep.isNull())
                    {
                        ins++;
                        StringBuffer gname("hthor__");
                        gname.append(name);
                        if (ins>1)
                            gname.append('_').append(ins);

                        IEspTpCluster* clusterInfo = createTpCluster("","");

                        clusterInfo->setName(gname.str());
                        clusterInfo->setQueueName(name);
                        clusterInfo->setDesc(cluster.queryProp("@build"));

                        clusterInfo->setBuild( cluster.queryProp("@description") );

                        StringBuffer path("/Environment/Software");
                        StringBuffer tmpPath;
                        setAttPath(path, ClusterType, "name", name, tmpPath);

                        clusterInfo->setType(ClusterType);
                        clusterInfo->setDirectory(insts->query().queryProp("@directory"));

                        StringBuffer tmpDir;
                        if (getConfigurationDirectory(root->queryPropTree("Software/Directories"), "run", ClusterType, name, tmpDir))
                        {
                            clusterInfo->setDirectory(tmpDir.str());
                        }
                        else
                        {
                            clusterInfo->setDirectory(insts->query().queryProp("@directory"));
                        }

                        clusterInfo->setPath(tmpPath.str());
                        clusterList.append(*clusterInfo);

                        //find out OS
                        OS_TYPE os = OS_WINDOWS;
                        const char* computer = insts->query().queryProp("@computer");
                        IPropertyTree* pHardware = root->queryPropTree("Hardware");
                        if (computer && *computer && pHardware)
                        {
                            StringBuffer xpath;
                            xpath.appendf("Computer[@name='%s']/@computerType", computer);
                            const char* computerType = pHardware->queryProp( xpath.str() );
                            if (computerType && *computerType)
                            {
                                xpath.clear().appendf("ComputerType[@name='%s']/@opSys", computerType);
                                const char* opSys = pHardware->queryProp( xpath.str() );
                                if (!stricmp(opSys, "linux") || !stricmp( opSys, "solaris"))
                                    os = OS_LINUX;
                            }
                        }
                        clusterInfo->setOS(os);
                    }
                }
            }
        }
    }
    catch(IException* e){   
        StringBuffer msg;
        e->errorMessage(msg);
        WARNLOG("%s", msg.str());
        e->Release();
    }
    catch(...){
        WARNLOG("Unknown Exception caught within CTpWrapper::getHthorClusterList");
    }
}


void CTpWrapper::getGroupList(double espVersion, const char* kindReq, IArrayOf<IEspTpGroup> &GroupList)
{
    try
    {
        Owned<IRemoteConnection> conn = querySDS().connect("/Groups", myProcessSession(), RTM_LOCK_READ, SDS_LOCK_TIMEOUT);
        Owned<IPropertyTreeIterator> groups= conn->queryRoot()->getElements("Group");
        if (groups->first())
        {
            do
            {
                IPropertyTree &group = groups->query();
                const char* kind = group.queryProp("@kind");
                if (kindReq && *kindReq && !strieq(kindReq, kind))
                    continue;

                IEspTpGroup* pGroup = createTpGroup("","");
                const char* name = group.queryProp("@name");
                pGroup->setName(name);
                if (kind && *kind && (espVersion >= 1.21))
                {
                    pGroup->setKind(kind);
                    pGroup->setReplicateOutputs(checkGroupReplicateOutputs(name, kind));
                }
                GroupList.append(*pGroup);
            } while (groups->next());
        }
    }
    catch(IException* e)
    {
        StringBuffer msg;
        e->errorMessage(msg);
        WARNLOG("%s", msg.str());
        e->Release();
    }
    catch(...)
    {
        WARNLOG("Unknown Exception caught within CTpWrapper::getGroupList");
    }
}

bool CTpWrapper::checkGroupReplicateOutputs(const char* groupName, const char* kind)
{
    if (strieq(kind, "Roxie") || strieq(kind, "hthor"))
        return false;

    Owned<IEnvironmentFactory> factory = getEnvironmentFactory(true);
    Owned<IConstEnvironment> environment = factory->openEnvironment();
    Owned<IPropertyTree> root = &environment->getPTree();

    Owned<IPropertyTreeIterator> it= root->getElements("Software/ThorCluster");
    ForEach(*it)
    {
        StringBuffer thorClusterGroupName;
        IPropertyTree& cluster = it->query();
        getClusterGroupName(cluster, thorClusterGroupName);
        if (thorClusterGroupName.length() && strieq(thorClusterGroupName.str(), groupName))
            return cluster.getPropBool("@replicateOutputs", false);
    }
    return false;
}

void CTpWrapper::resolveGroupInfo(const char* groupName,StringBuffer& Cluster, StringBuffer& ClusterPrefix)
{
    if(*groupName == 0)
    {
        DBGLOG("NULL PARAMETER groupName");
        return;
    }
    //There is a big estimate here.... namely that one group can only be associated with one cluster.......
    // if this changes then this code may be invalidated....
    try
    {
        Owned<IPropertyTree> pTopology = getEnvironment("Software/Topology");
        if (!pTopology)
            throw MakeStringExceptionDirect(ECLWATCH_CANNOT_GET_ENV_INFO, MSG_FAILED_GET_ENVIRONMENT_INFO);

        Owned<IPropertyTreeIterator> nodes=  pTopology->getElements("//Cluster");
        if (nodes->first()) 
        {
            do 
            {

                IPropertyTree &node = nodes->query();
                if (ContainsProcessDefinition(node,groupName)==true)
                {
                    //the prefix info is contained within the parent
                    ClusterPrefix.append(node.queryProp("@prefix"));
                    Cluster.append(node.queryProp("@name"));
                    break;
                }
            } while (nodes->next());
        }
    }
    catch(IException* e){   
      StringBuffer msg;
      e->errorMessage(msg);
        WARNLOG("%s", msg.str());
        e->Release();
    }
    catch(...){
        WARNLOG("Unknown Exception caught within CTpWrapper::resolveGroupInfo");
    }
}

bool CTpWrapper::ContainsProcessDefinition(IPropertyTree& clusterNode,const char* clusterName)
{
    Owned<IPropertyTreeIterator> processNodes = clusterNode.getElements("*");
    if (processNodes->first()) {
        do {
            IPropertyTree &node = processNodes->query();
            const char* processName = node.queryProp("@process");
            if (*processName > 0 && (strcmp(processName,clusterName) == 0))
                return true;
        } while (processNodes->next());
        }
    return false;
}


void CTpWrapper::getMachineInfo(double clientVersion, const char* name, const char* netAddress, IEspTpMachine& machineInfo)
{
    Owned<IEnvironmentFactory> envFactory = getEnvironmentFactory(true);
    Owned<IConstEnvironment> constEnv = envFactory->openEnvironment();
    Owned<IConstMachineInfo> pMachineInfo;
    if (name && *name)
        pMachineInfo.setown(constEnv->getMachine(name));
    else if (netAddress && *netAddress)
        pMachineInfo.setown(constEnv->getMachineByAddress(netAddress));
    else
        throw MakeStringException(ECLWATCH_CANNOT_GET_ENV_INFO, "Machine not specified");

    if (!pMachineInfo)
        throw MakeStringException(ECLWATCH_CANNOT_GET_ENV_INFO, "Machine Not Found for %s '%s'",
            (name && *name)? "Name" : "Net Address", (name && *name)? name : netAddress);

    setTpMachine(pMachineInfo, machineInfo);
}

void CTpWrapper::setTpMachine(IConstMachineInfo* machine, IEspTpMachine& tpMachine)
{
    if (!machine)
        return;

    SCMStringBuffer machineName, netAddress;
    machine->getName(machineName);
    machine->getNetAddress(netAddress);
    tpMachine.setName(machineName.str());
    tpMachine.setNetaddress(netAddress.str());
    tpMachine.setOS(machine->getOS());

    switch(machine->getState())
    {
        case MachineStateAvailable:
            tpMachine.setAvailable("Available");
            break;
        case MachineStateUnavailable:
            tpMachine.setAvailable("Unavailable");
            break;
        case MachineStateUnknown:
            tpMachine.setAvailable("Unknown");
            break;
    }
    Owned<IConstDomainInfo> pDomain = machine->getDomain();
    if (pDomain != 0)
    {
        SCMStringBuffer sName;
        tpMachine.setDomain(pDomain->getName(sName).str());
    }
}

void CTpWrapper::getMachineInfo(IEspTpMachine& machineInfo,IPropertyTree& machine,const char* ParentPath,const char* MachineType,const char* nodenametag)
{
    const char* name = machine.queryProp(nodenametag);
    setMachineInfo(name,MachineType,machineInfo);

    StringBuffer tmpPath;
    StringBuffer ppath(ParentPath);
    setAttPath(ppath,machine.queryName(),"name",name,tmpPath);
    machineInfo.setPath(tmpPath.str());
}

bool CTpWrapper::checkMultiSlavesFlag(const char* clusterName)
{
    Owned<IEnvironmentFactory> envFactory = getEnvironmentFactory(true);
    Owned<IConstEnvironment> constEnv = envFactory->openEnvironment();
    Owned<IPropertyTree> root = &constEnv->getPTree();

    VStringBuffer path("Software/ThorCluster[@name=\"%s\"]", clusterName);
    Owned<IPropertyTree> cluster= root->getPropTree(path.str());
    if (!cluster)
        throw MakeStringExceptionDirect(ECLWATCH_CANNOT_GET_ENV_INFO, MSG_FAILED_GET_ENVIRONMENT_INFO);

    //set this flag for legacy multi slave clusters because SwapNode made little sense in the old scheme
    //This is no longer an option in new environments, but is kept for backward compatibility with old
    //multi slave environments that used to list multiple slaves per node manually.
    return cluster->getPropBool("@multiSlaves");
}

void CTpWrapper::appendMachineList(double clientVersion, IConstEnvironment* constEnv, INode& node, const char* clusterName,
    const char* machineType, unsigned& processNumber, const char* directory, IArrayOf<IEspTpMachine>& machineList)
{
    StringBuffer netAddress;
    node.endpoint().getIpText(netAddress);
    if (netAddress.length() == 0)
    {
        WARNLOG("Net address not found for a node of %s", clusterName);
        return;
    }

    processNumber++;

    Owned<IEspTpMachine> machineInfo = createTpMachine("","");
    machineInfo->setType(machineType);
    machineInfo->setNetaddress(netAddress.str());
    if (!isEmptyString(directory))
        machineInfo->setDirectory(directory);

    Owned<IConstMachineInfo> pMachineInfo =  constEnv->getMachineByAddress(netAddress.str());
    if (pMachineInfo.get())
    {
        setTpMachine(pMachineInfo, *machineInfo);
        if (clientVersion > 1.17)
        {
            machineInfo->setProcessNumber(processNumber);
        }
    }
    else
    {
        machineInfo->setName("external");
        machineInfo->setOS(MachineOsUnknown);
    }

    machineList.append(*machineInfo.getLink());
}

void CTpWrapper::getThorSlaveMachineList(double clientVersion, const char* clusterName, const char* directory, IArrayOf<IEspTpMachine>& machineList)
{
    try
    {
        Owned<IEnvironmentFactory> envFactory = getEnvironmentFactory(true);
        Owned<IConstEnvironment> constEnv = envFactory->openEnvironment();
        Owned<IGroup> nodeGroup = getClusterProcessNodeGroup(clusterName, "ThorCluster");
        if (!nodeGroup || (nodeGroup->ordinality() == 0))
            return;

        unsigned processNumber = 0;
        Owned<INodeIterator> gi = nodeGroup->getIterator();
        ForEach(*gi)
            appendMachineList(clientVersion, constEnv, gi->query(), clusterName, eqThorSlaveProcess, processNumber, directory, machineList);
    }
    catch(IException* e)
    {
        StringBuffer msg;
        e->errorMessage(msg);
        WARNLOG("%s", msg.str());
        e->Release();
    }
    catch(...)
    {
        WARNLOG("Unknown Exception caught within CTpWrapper::getMachineList");
    }

    return;
}

void CTpWrapper::getThorSpareMachineList(double clientVersion, const char* clusterName, const char* directory, IArrayOf<IEspTpMachine>& machineList)
{
    try
    {
        Owned<IEnvironmentFactory> envFactory = getEnvironmentFactory(true);
        Owned<IConstEnvironment> constEnv = envFactory->openEnvironment();
        Owned<IPropertyTree> root = &constEnv->getPTree();

        VStringBuffer path("Software/ThorCluster[@name=\"%s\"]", clusterName);
        Owned<IPropertyTree> cluster= root->getPropTree(path.str());
        if (!cluster)
            throw MakeStringExceptionDirect(ECLWATCH_CANNOT_GET_ENV_INFO, MSG_FAILED_GET_ENVIRONMENT_INFO);

        StringBuffer groupName;
        getClusterSpareGroupName(*cluster, groupName);
        if (groupName.length() < 1)
            return;

        Owned<IGroup> nodeGroup = queryNamedGroupStore().lookup(groupName.str());
        if (!nodeGroup || (nodeGroup->ordinality() == 0))
            return;

        unsigned processNumber = 0;
        Owned<INodeIterator> gi = nodeGroup->getIterator();
        ForEach(*gi)
            appendMachineList(clientVersion, constEnv, gi->query(), clusterName, eqThorSlaveProcess, processNumber, directory, machineList);
    }
    catch(IException* e)
    {
        StringBuffer msg;
        e->errorMessage(msg);
        WARNLOG("%s", msg.str());
        e->Release();
    }
    catch(...)
    {
        WARNLOG("Unknown Exception caught within CTpWrapper::getMachineList");
    }

    return;
}

void CTpWrapper::getMachineList(const char* MachineType,
                                const char* ParentPath,
                                const char* Status,
                                          const char* Directory,
                                IArrayOf<IEspTpMachine> &MachineList,
                                set<string>* pMachineNames/*=NULL*/)
{
    try
    {
        //ParentPath=Path to parent node... normally a cluster
        Owned<IEnvironmentFactory> envFactory = getEnvironmentFactory(true);
        Owned<IConstEnvironment> constEnv = envFactory->openEnvironment();
        Owned<IPropertyTree> root0 = &constEnv->getPTree();
    
        char* xpath = (char*)ParentPath;
        if (!strnicmp(xpath, "/Environment/", 13))
            xpath += 13;

        IPropertyTree* root = root0->queryPropTree( xpath );
        if (!root)
            throw MakeStringExceptionDirect(ECLWATCH_CANNOT_GET_ENV_INFO, MSG_FAILED_GET_ENVIRONMENT_INFO);

        Owned<IPropertyTreeIterator> machines= root->getElements(MachineType);
        const char* nodenametag = getNodeNameTag(MachineType);
        if (machines->first()) {
            do {

                IPropertyTree &machine = machines->query();

            if (pMachineNames)//caller wishes us to avoid inserting duplicate entries for machines
            {
               const char* machineName = machine.queryProp(nodenametag);
               if (pMachineNames->find(machineName) != pMachineNames->end())
                  continue;
               pMachineNames->insert(machineName);
            }

                //load up the machines of which we do not care what status is set or we have a matching status
                const char* state = machine.queryProp("@state");
            if ((Status==NULL || *Status=='\0')  || 
                (state && strcmp(Status, state)==0)) 
                {
                    IEspTpMachine & machineInfo = *(createTpMachine("",""));
                    getMachineInfo(machineInfo,machine,ParentPath,MachineType,nodenametag);

                    if (Directory && *Directory)
                        machineInfo.setDirectory(Directory);

                    MachineList.append(machineInfo);
                }
            } while (machines->next());
        }
    }
    catch(IException* e){   
      StringBuffer msg;
      e->errorMessage(msg);
        WARNLOG("%s", msg.str());
        e->Release();
    }
    catch(...){
        WARNLOG("Unknown Exception caught within CTpWrapper::getMachineList");
    }

}

const char* CTpWrapper::getNodeNameTag(const char* MachineType)
{
    if (strcmp(MachineType,"Computer")==0)
        return "@name";
    else
        return "@computer";
}

void CTpWrapper::getDropZoneMachineList(double clientVersion, bool ECLWatchVisibleOnly, IArrayOf<IEspTpMachine> &MachineList)
{
    try
    {
        IArrayOf<IConstTpDropZone> list;
        getTpDropZones(clientVersion, nullptr, ECLWatchVisibleOnly, list);
        ForEachItemIn(i, list)
        {
            IConstTpDropZone& dropZone = list.item(i);

            IArrayOf<IConstTpMachine>& tpMachines = dropZone.getTpMachines();
            ForEachItemIn(ii, tpMachines)
            {
                IConstTpMachine& tpMachine = tpMachines.item(ii);
                Owned<IEspTpMachine> machine = createTpMachine();
                machine->copy(tpMachine);

                MachineList.append(*machine.getLink());
            }
        }
    }
    catch(IException* e)
    {
        EXCLOG(e);
        e->Release();
    }
    catch(...)
    {
        WARNLOG("Unknown Exception caught within CTpWrapper::getDropZoneMachineList");
    }
    
}

//For a given dropzone or every dropzones (check ECLWatchVisible if needed), read: "@name",
// "@description", "@build", "@directory", "@ECLWatchVisible" into an IEspTpDropZone object.
//For each ServerList, read "@name" and "@server" (hostname or IP) into an IEspTpMachine object.
//Add the IEspTpMachine object into the IEspTpDropZone.

void CTpWrapper::getTpDropZones(double clientVersion, const char* name, bool ECLWatchVisibleOnly, IArrayOf<IConstTpDropZone>& list)
{
    Owned<IEnvironmentFactory> envFactory = getEnvironmentFactory(true);
    Owned<IConstEnvironment> constEnv = envFactory->openEnvironment();
    if (!isEmptyString(name))
    {
        Owned<IConstDropZoneInfo> pDropZoneInfo = constEnv->getDropZone(name);
        if (pDropZoneInfo && (!ECLWatchVisibleOnly || pDropZoneInfo->isECLWatchVisible()))
            appendTpDropZone(clientVersion, constEnv, *pDropZoneInfo, list);
    }
    else
    {
        Owned<IConstDropZoneInfoIterator> it = constEnv->getDropZoneIterator();
        ForEach(*it)
        {
            IConstDropZoneInfo& dropZoneInfo = it->query();
            if (!ECLWatchVisibleOnly || dropZoneInfo.isECLWatchVisible())
                appendTpDropZone(clientVersion, constEnv, dropZoneInfo, list);
        }
    }
}


void CTpWrapper::appendTpDropZone(double clientVersion, IConstEnvironment* constEnv, IConstDropZoneInfo& dropZoneInfo, IArrayOf<IConstTpDropZone>& list)
{
    SCMStringBuffer dropZoneName, description, directory, umask, build, computer;
    dropZoneInfo.getName(dropZoneName);
    dropZoneInfo.getDescription(description);
    dropZoneInfo.getDirectory(directory);
    dropZoneInfo.getUMask(umask);
    dropZoneInfo.getComputerName(computer);

    Owned<IEspTpDropZone> dropZone = createTpDropZone();
    if (dropZoneName.length() > 0)
        dropZone->setName(dropZoneName.str());
    if (description.length() > 0)
        dropZone->setDescription(description.str());
    if (directory.length() > 0)
        dropZone->setPath(directory.str());
    if (build.length() > 0)
        dropZone->setBuild(build.str());
    dropZone->setECLWatchVisible(dropZoneInfo.isECLWatchVisible());

    IArrayOf<IEspTpMachine> tpMachines;
    Owned<IConstDropZoneServerInfoIterator> itr = dropZoneInfo.getServers();
    ForEach(*itr)
    {
        IConstDropZoneServerInfo& dropZoneServer = itr->query();

        StringBuffer name, server, networkAddress;
        dropZoneServer.getName(name);
        dropZoneServer.getServer(server);
        if (name.isEmpty() && server.isEmpty())
            continue;

        Owned<IEspTpMachine> machine = createTpMachine();
        if (!name.isEmpty())
            machine->setName(name.str());
        if (!server.isEmpty())
        {
            IpAddress ipAddr;
            ipAddr.ipset(server.str());
            ipAddr.getIpText(networkAddress);
            machine->setNetaddress(networkAddress.str());
            machine->setConfigNetaddress(server.str());
        }
        if (directory.length() > 0)
        {
            machine->setDirectory(directory.str());
            machine->setOS(getPathSepChar(directory.str()) == '/' ? MachineOsLinux : MachineOsW2K);
        }
        tpMachines.append(*machine.getLink());
    }
    dropZone->setTpMachines(tpMachines);

    list.append(*dropZone.getLink());
}

IEspTpMachine* CTpWrapper::createTpMachineEx(const char* name, const char* type, IConstMachineInfo* machineInfo)
{
    if (!machineInfo)
        return nullptr;

    Owned<IEspTpMachine> machine = createTpMachine();
    machine->setName(name);
    machine->setType(type);
    machine->setOS(machineInfo->getOS());

    Owned<IConstDomainInfo> domain = machineInfo->getDomain();
    if (domain)
    {
        SCMStringBuffer sName;
        machine->setDomain(domain->getName(sName).str());
    }

    SCMStringBuffer netAddr;
    machineInfo->getNetAddress(netAddr);
    if (netAddr.length() > 0)
    {
        StringBuffer networkAddress;
        IpAddress ipAddr;
        ipAddr.ipset(netAddr.str());
        ipAddr.getIpText(networkAddress);
        machine->setNetaddress(networkAddress.str());
    }

    switch(machineInfo->getState())
    {
        case MachineStateAvailable:
            machine->setAvailable("Available");
            break;
        case MachineStateUnavailable:
            machine->setAvailable("Unavailable");
            break;
        default:
            machine->setAvailable("Unknown");
            break;
    }
    return machine.getClear();
}

void CTpWrapper::setMachineInfo(const char* name,const char* type,IEspTpMachine& machine)
{
    try{
        Owned<IEnvironmentFactory> factory = getEnvironmentFactory(true);
        Owned<IConstEnvironment> constEnv = factory->openEnvironment();
        Owned<IConstMachineInfo> pMachineInfo =  constEnv->getMachine(name);
        if (pMachineInfo.get())
        {
            SCMStringBuffer ep;

            pMachineInfo->getNetAddress(ep);

            const char* ip = ep.str();
            if (!ip || stricmp(ip, "."))
            {
                machine.setNetaddress(ep.str());
                machine.setConfigNetaddress(ep.str());
            }
            else
            {
                StringBuffer ipStr;
                IpAddress ipaddr = queryHostIP();
                ipaddr.getIpText(ipStr);
                if (ipStr.length() > 0)
                {
#ifdef MACHINE_IP
                    machine.setNetaddress(MACHINE_IP);
#else
                    machine.setNetaddress(ipStr.str());
#endif
                    machine.setConfigNetaddress(".");
                }
            }
            machine.setOS(pMachineInfo->getOS());
                
            
            switch(pMachineInfo->getState())
            {
                case MachineStateAvailable:
                    machine.setAvailable("Available");
                    break;
                case MachineStateUnavailable:
                    machine.setAvailable("Unavailable");
                    break;
                case MachineStateUnknown:
                    machine.setAvailable("Unknown");
                    break;
            }
            Owned<IConstDomainInfo> pDomain = pMachineInfo->getDomain();
            if (pDomain != 0)
            {
                SCMStringBuffer sName;
                machine.setDomain(pDomain->getName(sName).str());
            }
        }
        machine.setName(name);
        machine.setType(type);
    }
    catch(IException* e){   
        StringBuffer msg;
        e->errorMessage(msg);
        WARNLOG("%s", msg.str());
        e->Release();
    }
    catch(...){
        WARNLOG("Unknown Exception caught within CTpWrapper::getDropZoneList");
    }
}

void CTpWrapper::setAttPath(StringBuffer& Path,const char* PathToAppend,const char* AttName,const char* AttValue,StringBuffer& returnStr)
{
    Path.append("/");
    Path.append(PathToAppend);
    Path.append("[@");
    Path.append(AttName);
    Path.append("=\"");
    Path.append(AttValue);
    Path.append("\"]");
    StringBuffer rawPath;
    const void* buff = (void*)Path.str();
    JBASE64_Encode(buff,Path.length(),rawPath, false);
    returnStr.append(rawPath.str());
}

void CTpWrapper::getAttPath(const char* Path,StringBuffer& returnStr)
{
    StringBuffer decodedStr;
    JBASE64_Decode(Path, returnStr);
}

