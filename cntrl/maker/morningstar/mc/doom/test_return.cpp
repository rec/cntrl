// SPDX-License-Identifier: GPL-2.0-or-later
#include "return_button.h"

#include <cassert>

int main()
{
    using Action = ReturnButton::Action;
    ReturnButton button;
    assert(button.sample(false, 0) == Action::none);
    assert(button.sample(true, 100) == Action::none);
    assert(button.sample(false, 200) == Action::use);
    assert(button.sample(false, 230) == Action::none);

    assert(button.sample(true, 1000) == Action::none);
    assert(button.sample(true, 2999) == Action::none);
    assert(button.sample(true, 3000) == Action::restart);

    ReturnButton wrapping;
    assert(wrapping.sample(true, UINT32_MAX - 1000) == Action::none);
    assert(wrapping.sample(false, 999) == Action::restart);
}
