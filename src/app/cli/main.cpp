/*
 *    Copyright (c) 2019, The OpenThread Commissioner Authors.
 *    All rights reserved.
 *
 *    Redistribution and use in source and binary forms, with or without
 *    modification, are permitted provided that the following conditions are met:
 *    1. Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *    2. Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *    3. Neither the name of the copyright holder nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 *    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *    AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *    IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *    ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 *    LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *    CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *    SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *    INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *    CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *    ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *    POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * @file
 *   The file is the entrance of the commissioner CLI.
 */

#include <signal.h>

#include "app/cli/interpreter.hpp"
#include "common/utils.hpp"

#ifndef OT_COMM_VERSION
#error "OT_COMM_VERSION not defined"
#endif

using namespace ot::commissioner;

/**
 * The OT-commissioner CLI logo.
 * Generated by http://patorjk.com/software/taag
 * with font=Slant and text="OT-commissioner CLI".
 */
static const std::string kLogo =
    R"(   ____  ______                                   _           _                          ________    ____)"
    "\n"
    R"(  / __ \/_  __/   _________  ____ ___  ____ ___  (_)_________(_)___  ____  ___  _____   / ____/ /   /  _/)"
    "\n"
    R"( / / / / / /_____/ ___/ __ \/ __ `__ \/ __ `__ \/ / ___/ ___/ / __ \/ __ \/ _ \/ ___/  / /   / /    / /  )"
    "\n"
    R"(/ /_/ / / /_____/ /__/ /_/ / / / / / / / / / / / (__  |__  ) / /_/ / / / /  __/ /     / /___/ /____/ /   )"
    "\n"
    R"(\____/ /_/      \___/\____/_/ /_/ /_/_/ /_/ /_/_/____/____/_/\____/_/ /_/\___/_/      \____/_____/___/   )"
    "\n"
    R"(                                                                                                         )"
    "\n";

static void PrintUsage(const std::string &aProgram)
{
    static const std::string usage = "usage: \n"
                                     "    " +
                                     aProgram + " <config-file>";

    Console::Write(usage, Console::Color::kWhite);
}

static void PrintVersion()
{
    Console::Write(OT_COMM_VERSION, Console::Color::kWhite);
}

static Interpreter gInterpreter;

static void HandleSignalInterrupt(int)
{
    gInterpreter.AbortCommand();
}

int main(int argc, const char *argv[])
{
    Error error;

    Config config;

    if (argc < 2 || ToLower(argv[1]) == "-h" || ToLower(argv[1]) == "--help")
    {
        PrintUsage(argv[0]);
        return 0;
    }
    else if (ToLower(argv[1]) == "-v" || ToLower(argv[1]) == "--version")
    {
        PrintVersion();
        return 0;
    }

    signal(SIGINT, HandleSignalInterrupt);

    Console::Write(kLogo, Console::Color::kBlue);

    SuccessOrExit(error = gInterpreter.Init(argv[1]));

    gInterpreter.Run();

exit:
    if (!error.NoError())
    {
        Console::Write("start OT-commissioner CLI failed: " + error.ToString(), Console::Color::kRed);
    }
    return error.NoError() ? 0 : -1;
}
