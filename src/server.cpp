// This file is part of xsonrpc, an XML/JSON RPC library.
// Copyright (C) 2015 Erik Johansson <erik@ejohansson.se
//
// This library is free software; you can redistribute it and/or modify it
// under the terms of the GNU Lesser General Public License as published by the
// Free Software Foundation; either version 2.1 of the License, or (at your
// option) any later version.
//
// This library is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License
// for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this library; if not, write to the Free Software Foundation,
// Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA

#include "server.h"

#include <microhttpd.h>
#include <tinyxml2.h>
#include <vector>

namespace {

const char TEXT_XML[] = "text/xml";
const size_t MAX_REQUEST_SIZE = 16 * 1024;

struct HttpError
{
  unsigned int StatusCode;
};

struct ConnectionInfo
{
  std::vector<char> Buffer;
  tinyxml2::XMLPrinter Printer;
};

} // namespace

namespace xsonrpc {

Server::Server(unsigned short port, std::string uri)
  : myUri(std::move(uri))
{
  myDaemon = MHD_start_daemon(
    MHD_USE_EPOLL_LINUX_ONLY,
    port, NULL, NULL, &Server::AccessHandlerCallback, this,
    MHD_OPTION_NOTIFY_COMPLETED, &Server::RequestCompletedCallback, this,
    MHD_OPTION_END);
  if (!myDaemon) {
    throw std::runtime_error("server: could not start HTTP daemon");
  }
}

Server::~Server()
{
  MHD_stop_daemon(myDaemon);
}

void Server::Run()
{
  if (MHD_run(myDaemon) != MHD_YES) {
    throw std::runtime_error("server: could not run HTTP daemon");
  }
}

int Server::GetFileDescriptor()
{
  auto info = MHD_get_daemon_info(
    myDaemon, MHD_DAEMON_INFO_EPOLL_FD_LINUX_ONLY);
  if (!info || info->listen_fd == -1) {
    throw std::runtime_error("server: could not get file descriptor");
  }
  return info->listen_fd;
}

void Server::OnReadableFileDescriptor()
{
  if (MHD_run(myDaemon) != MHD_YES) {
    throw std::runtime_error("server: invalid call to run daemon");
  }
}

void Server::HandleRequest(MHD_Connection* connection, void* connectionCls)
{
  auto info = static_cast<ConnectionInfo*>(connectionCls);

  try {
    tinyxml2::XMLDocument document;
    auto error = document.Parse(info->Buffer.data(), info->Buffer.size());
    if (error == tinyxml2::XML_CAN_NOT_CONVERT_TEXT) {
      throw InvalidCharacterFault();
    }
    else if (error != tinyxml2::XML_NO_ERROR) {
      throw NotWellFormedFault();
    }
    info->Buffer.clear();

    Request request{document.RootElement()};
    document.Clear();
    auto response = myDispatcher.Invoke(
      request.GetMethodName(), request.GetParameters());
    response.Print(info->Printer);
  }
  catch (const Fault& ex) {
    Response(ex).Print(info->Printer);
  }

  auto response = MHD_create_response_from_buffer(
    info->Printer.CStrSize() - 1,
    const_cast<char*>(info->Printer.CStr()),
    MHD_RESPMEM_PERSISTENT);

  MHD_add_response_header(response, MHD_HTTP_HEADER_CONTENT_TYPE, TEXT_XML);
  MHD_add_response_header(response, MHD_HTTP_HEADER_SERVER,
                          "xsonrpc/" XSONRPC_VERSION);
  MHD_queue_response(connection, MHD_HTTP_OK, response);
  MHD_destroy_response(response);
}

int Server::AccessHandlerCallback(
  void* cls, MHD_Connection* connection,
  const char* url, const char* method, const char* version,
  const char* uploadData, size_t* uploadDataSize,
  void** connectionCls)
{
  return static_cast<Server*>(cls)->AccessHandler(
    connection, url, method, version, uploadData, uploadDataSize,
    connectionCls);
}

int Server::AccessHandler(
  MHD_Connection* connection,
  const char* url, const char* method, const char* version,
  const char* uploadData, size_t* uploadDataSize,
  void** connectionCls)
{
  try {
    if (*connectionCls != NULL) {
      if (*uploadDataSize == 0) {
        HandleRequest(connection, *connectionCls);
        return MHD_YES;
      }

      ConnectionInfo* info = static_cast<ConnectionInfo*>(*connectionCls);
      if (info->Buffer.size() + *uploadDataSize > MAX_REQUEST_SIZE) {
        *uploadDataSize = 0;
        throw HttpError{MHD_HTTP_BAD_REQUEST};
      }
      info->Buffer.insert(info->Buffer.end(), uploadData,
                          uploadData + *uploadDataSize);
      *uploadDataSize = 0;
      return MHD_YES;
    }

    if (strcmp(method, "POST") != 0) {
      throw HttpError{MHD_HTTP_METHOD_NOT_ALLOWED};
    }
    if (url != myUri) {
      throw HttpError{MHD_HTTP_NOT_FOUND};
    }

    *connectionCls = new ConnectionInfo;
    return MHD_YES;
  }
  catch (const HttpError& httpError) {
    auto response = MHD_create_response_from_data(0, nullptr, false, false);
    MHD_queue_response(connection, httpError.StatusCode, response);
    MHD_destroy_response(response);
    return MHD_YES;
  }
  catch (...) {
    auto response = MHD_create_response_from_data(0, nullptr, false, false);
    MHD_queue_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, response);
    MHD_destroy_response(response);
    return MHD_YES;
  }
}

void Server::RequestCompletedCallback(
  void* cls, MHD_Connection* connection,
  void** connectionCls, int requestTerminationCode)
{
  static_cast<Server*>(cls)->OnRequestCompleted(
    connection, connectionCls, requestTerminationCode);
}

void Server::OnRequestCompleted(
  MHD_Connection* connection, void** connectionCls,
  int requestTerminationCode)
{
  delete static_cast<ConnectionInfo*>(*connectionCls);
}

} // namespace xsonrpc