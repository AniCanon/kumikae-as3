# ProbeApp

A small UIKit app used only by the helper's [probes](../README.md#probes). A probe compiles it with the
installed Simulator SDK and runs it on the disposable device that the probe creates.

The app writes a JSON report to its Documents folder: the text it received, its interface orientation, its
drawing ticks and its touch callbacks. A probe compares that report with what it sent.

The app needs no package, signing account or network. The helper uses none of its code.
