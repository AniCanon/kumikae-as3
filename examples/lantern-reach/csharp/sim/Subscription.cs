using System;
using System.Collections.Generic;

namespace Kumikae.LanternReach;

public sealed class Subscription {
    public bool active = true;
    public Action<WorldEvent> notify;
}
