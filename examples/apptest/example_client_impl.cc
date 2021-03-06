// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "examples/apptest/example_client_impl.h"

namespace mojo {

ExampleClientImpl::ExampleClientImpl() : last_pong_value_(0) {}

ExampleClientImpl::~ExampleClientImpl() {}

void ExampleClientImpl::Pong(uint16_t pong_value) {
  last_pong_value_ = pong_value;
}

}  // namespace mojo
