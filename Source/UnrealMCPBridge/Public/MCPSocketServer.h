// Copyright Omar Abdelwahed 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "HAL/ThreadSafeBool.h"

/**
 * Socket server for MCP communications
 */
class FMCPSocketServer : public FRunnable
{
public:
    FMCPSocketServer();
    virtual ~FMCPSocketServer();

    // FRunnable interface
    virtual bool Init() override;
    virtual uint32 Run() override;
    virtual void Stop() override;
    virtual void Exit() override;

    // Server control
    void Start();
    void HandleClientConnection(FSocket* ClientSocket);
    void ProcessClientMessage(FSocket* ClientSocket, const FString& Message);

private:
    FSocket* ListenerSocket;
    FRunnableThread* Thread;
    FThreadSafeBool bStopping;

    // Server configuration
    int32 Port;
    FString ListenAddress;
};
