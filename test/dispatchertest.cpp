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

#include "dispatcher.h"

#include <catch.hpp>

using namespace xsonrpc;

namespace {

Value TestMethod(const Request::Parameters& params)
{
  if (params.empty()) {
    return true;
  }
  return Value(params[0]);
}

} // namespace

TEST_CASE("method wrapper")
{
  MethodWrapper method(&TestMethod);

  CHECK_FALSE(method.HasHelpText());
  method.SetHelpText("test help");
  CHECK(method.HasHelpText());
  CHECK(method.GetHelpText() == "test help");

  method.AddSignature(Value::Type::BOOLEAN);
  method.AddSignature(Value::Type::STRING, Value::Type::STRING);
  method.AddSignature(Value::Type::INTEGER_32, Value::Type::STRING,
                      Value::Type::BOOLEAN);

  auto& signatures = method.GetSignatures();
  REQUIRE(signatures.size() == 3);
  CHECK(signatures[0] == std::vector<Value::Type>{Value::Type::BOOLEAN});
  CHECK(signatures[1] == (std::vector<Value::Type>{
        Value::Type::STRING, Value::Type::STRING}));
  CHECK(signatures[2] == (std::vector<Value::Type>{
        Value::Type::INTEGER_32, Value::Type::STRING, Value::Type::BOOLEAN}));

  Request::Parameters params;
  CHECK(method(params).AsBoolean());

  params.emplace_back("foobar");
  CHECK(method(params).AsString() == "foobar");
}

TEST_CASE("dispatcher")
{
  Dispatcher dispatcher;
  dispatcher.AddMethod("test", &TestMethod);

  {
    auto response = dispatcher.Invoke("test", {});
    CHECK(response.GetResult().AsBoolean());
  }

  dispatcher.RemoveMethod("test");

  {
    auto response = dispatcher.Invoke("test", {});
    CHECK(response.IsFault());
  }
}