// Copyright Omar Abdelwahed 2025. All Rights Reserved.

#include "MCPSocketServer.h"
#include "PythonBridge.h"
#include "SocketSubsystem.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "HAL/RunnableThread.h"
#include "JsonGlobals.h"
#include "JsonObjectConverter.h"
#include <PythonScriptPlugin/Private/PythonScriptRemoteExecution.h>
#include <Common/TcpSocketBuilder.h>

FMCPSocketServer::FMCPSocketServer()
    : ListenerSocket(nullptr)
    , Thread(nullptr)
    , bStopping(false)
    , Port(9000)
    , ListenAddress(TEXT("127.0.0.1"))
{
}

FMCPSocketServer::~FMCPSocketServer()
{
    if (Thread)
    {
        Thread->Kill(true);
        delete Thread;
    }

    if (ListenerSocket)
    {
        ListenerSocket->Close();
        ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(ListenerSocket);
    }
}

bool FMCPSocketServer::Init()
{
    return true;
}

void FMCPSocketServer::Start()
{
    if (!Thread)
    {
        Thread = FRunnableThread::Create(this, TEXT("MCPSocketServer"), 0, TPri_Normal);
    }
}

uint32 FMCPSocketServer::Run()
{
    FIPv4Address IPAddress;
    FIPv4Address::Parse(ListenAddress, IPAddress);

    // Create the socket
    FIPv4Endpoint Endpoint(IPAddress, Port);
    ListenerSocket = FTcpSocketBuilder(TEXT("MCPListenerSocket"))
        .AsReusable()
        .BoundToEndpoint(Endpoint)
        .Listening(8);

    if (!ListenerSocket)
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to create listener socket"));
        return 0;
    }

    UE_LOG(LogTemp, Display, TEXT("MCP Socket Server listening on %s:%d"), *ListenAddress, Port);

    // Set socket to non-blocking mode
    ListenerSocket->SetNonBlocking(true);

    // Main server loop
    while (!bStopping)
    {
        bool bHasPendingConnection = false;
        ListenerSocket->HasPendingConnection(bHasPendingConnection);

        if (bHasPendingConnection)
        {
            TSharedRef<FInternetAddr> RemoteAddress = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();
            FSocket* ClientSocket = ListenerSocket->Accept(*RemoteAddress, TEXT("MCP Client Connection"));

            if (ClientSocket)
            {
                UE_LOG(LogTemp, Display, TEXT("MCP Client connected from %s"), *RemoteAddress->ToString(true));

                // Handle the client in the same thread for simplicity
                // In a production environment, you'd likely want to spawn a new thread for each client
                HandleClientConnection(ClientSocket);
            }
        }

        // Sleep to prevent tight loop
        FPlatformProcess::Sleep(0.1f);
    }

    return 0;
}

void FMCPSocketServer::Stop()
{
    bStopping = true;
}

void FMCPSocketServer::Exit()
{
    if (ListenerSocket)
    {
        ListenerSocket->Close();
        ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(ListenerSocket);
        ListenerSocket = nullptr;
    }
}

void FMCPSocketServer::HandleClientConnection(FSocket* ClientSocket)
{
    /*if (!ClientSocket)
    {
        return;
    }*/
    UE_LOG(LogTemp, Display, TEXT("FMCPSocketServer::HandleClientConnection"));

    // Set socket to non-blocking mode
    ClientSocket->SetNonBlocking(true);

    // Buffer for incoming data
    TArray<uint8> RecvBuffer;
    RecvBuffer.SetNumUninitialized(1024 * 16); // 16KB buffer

    while (!bStopping)
    {
        uint32 PendingDataSize = 0;
        if (ClientSocket->HasPendingData(PendingDataSize))
        {
            UE_LOG(LogTemp, Display, TEXT("... client sending data!"));
            int32 BytesRead = 0;
            if (ClientSocket->Recv(RecvBuffer.GetData(), RecvBuffer.Num(), BytesRead))
            {
                if (BytesRead > 0)
                {
                    UE_LOG(LogTemp, Display, TEXT("...bytes: %i"), BytesRead);
                    // Add null terminator
                    RecvBuffer[BytesRead] = 0;

                    // Convert to FString and process
                    FString ReceivedData = UTF8_TO_TCHAR(reinterpret_cast<ANSICHAR*>(RecvBuffer.GetData()));
                    ProcessClientMessage(ClientSocket, ReceivedData);
                }
            }
        }

        // Sleep to prevent tight loop
        FPlatformProcess::Sleep(0.01f);
    }

    // Close and destroy the socket when done
    ClientSocket->Close();
    ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(ClientSocket);
}

void FMCPSocketServer::ProcessClientMessage(FSocket* ClientSocket, const FString& Message)
{
    UE_LOG(LogTemp, Display, TEXT("Received message: %s"), *Message);

    TSharedPtr<FJsonObject> JsonObject;
    TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(Message);

    if (FJsonSerializer::Deserialize(JsonReader, JsonObject) && JsonObject.IsValid())
    {
        FString Command = JsonObject->GetStringField(TEXT("command"));
        TSharedPtr<FJsonObject> Params = JsonObject->GetObjectField(TEXT("params"));

        // Process the command through the Python bridge
        FString Response = FPythonBridge::ExecuteCommand(Command, Params);

        // Send the response back to the client
        FTCHARToUTF8 ConvertToUTF8(*Response);
        int32 BytesSent = 0;
        ClientSocket->Send((uint8*)ConvertToUTF8.Get(), ConvertToUTF8.Length(), BytesSent);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to parse JSON message"));

        // Send error response
        FString ErrorResponse = TEXT("{\"status\":\"error\",\"message\":\"Invalid JSON format\"}");
        FTCHARToUTF8 ConvertToUTF8(*ErrorResponse);
        int32 BytesSent = 0;
        ClientSocket->Send((uint8*)ConvertToUTF8.Get(), ConvertToUTF8.Length(), BytesSent);
    }
}