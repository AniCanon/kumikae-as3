# Owned Simulator probe app

A Swift/UIKit test fixture, compiled only with the installed simulator SDK by the
opt-in native probe. It runs exclusively on the probe's disposable device. No
package dependency, signing account, host window or network is needed. Its JSON
report in Documents records actual text, interface orientation, drawing ticks and
touch callbacks. The native helper uses none of this test app's code.
