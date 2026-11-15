import UIKit

@main
final class ProbeApp: UIResponder, UIApplicationDelegate {
  var window: UIWindow?
  func application(_ application: UIApplication,
                   didFinishLaunchingWithOptions options: [UIApplication.LaunchOptionsKey: Any]?) -> Bool {
    let window = UIWindow(frame: UIScreen.main.bounds)
    window.rootViewController = ProbeController()
    window.makeKeyAndVisible()
    self.window = window
    return true
  }
}

final class ProbeController: UIViewController, UITextViewDelegate {
  private let picture = ProbePicture()
  private let editor = UITextView()
  private var ticks = 0
  private let target = UIButton(type: .custom)
  private var hits = 0
  private var transitioning = false
  private var link: CADisplayLink?
  override var supportedInterfaceOrientations: UIInterfaceOrientationMask { .all }
  override var shouldAutorotate: Bool { true }

  override func viewDidLoad() {
    super.viewDidLoad()
    view = picture
    editor.font = .systemFont(ofSize: 24)
    // Keep the fixture visible; native hardware key and paste actions still reach this responder.
    editor.inputView = UIView(frame: .zero)
    editor.autocorrectionType = .no
    editor.spellCheckingType = .no
    editor.smartQuotesType = .no
    editor.smartDashesType = .no
    editor.delegate = self
    picture.addSubview(editor)
    target.backgroundColor = .red
    target.addTarget(self, action: #selector(hit), for: .touchUpInside)
    picture.addSubview(target)
    let link = CADisplayLink(target: self, selector: #selector(tick))
    link.preferredFrameRateRange = CAFrameRateRange(minimum: 60, maximum: 60, preferred: 60)
    link.add(to: .main, forMode: .common)
    self.link = link
    UIDevice.current.beginGeneratingDeviceOrientationNotifications()
  }
  override func viewWillTransition(to size: CGSize, with coordinator: any UIViewControllerTransitionCoordinator) {
    transitioning = true
    report()
    super.viewWillTransition(to: size, with: coordinator)
    coordinator.animate(alongsideTransition: nil) { [weak self] _ in
      self?.transitioning = false
      self?.report()
    }
  }
  override func viewDidLayoutSubviews() {
    super.viewDidLayoutSubviews()
    editor.frame = CGRect(x: 30, y: 100, width: view.bounds.width - 60, height: 120)
    target.frame = CGRect(x: view.bounds.width - 80, y: 70, width: 30, height: 30)
    report()
  }
  @objc private func hit() { hits += 1; report() }
  override func viewDidAppear(_ animated: Bool) {
    super.viewDidAppear(animated)
    editor.becomeFirstResponder()
  }
  func textViewDidChange(_ textView: UITextView) { report() }
  @objc private func tick() {
    ticks += 1
    picture.tick = ticks
    picture.setNeedsDisplay()
    if ticks % 60 == 0 { report() }
  }
  private func report() {
    let value: [String: Any] = ["text": editor.text ?? "", "ticks": ticks,
      "draws": picture.draws, "touches": picture.touches, "hits": hits,
      "width": view.bounds.width, "height": view.bounds.height, "transitioning": transitioning,
      "uptime": ProcessInfo.processInfo.systemUptime,
      "orientation": view.window?.windowScene?.interfaceOrientation.rawValue ?? 0,
      "deviceOrientation": UIDevice.current.orientation.rawValue]
    do {
      let directory = try FileManager.default.url(for: .documentDirectory, in: .userDomainMask,
                                                 appropriateFor: nil, create: true)
      try JSONSerialization.data(withJSONObject: value).write(to: directory.appendingPathComponent("probe.json"), options: .atomic)
    } catch { fatalError("Probe report failed: \(error)") }
  }
}

final class ProbePicture: UIView {
  var tick = 0
  var draws = 0
  var touches = 0
  override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) { self.touches += touches.count }
  override func draw(_ rect: CGRect) {
    draws += 1
    UIColor.white.setFill()
    UIRectFill(bounds)
    UIColor.systemBlue.setFill()
    UIRectFill(CGRect(x: CGFloat(tick % 200), y: 250, width: 60, height: 120))
    for bit in 0..<16 {
      (tick & (1 << bit) == 0 ? UIColor.black : UIColor.white).setFill()
      UIRectFill(CGRect(x: 10 + bit * 10, y: 430, width: 10, height: 10))
      (tick & (1 << bit) == 0 ? UIColor.white : UIColor.black).setFill()
      UIRectFill(CGRect(x: 10 + bit * 10, y: 440, width: 10, height: 10))
    }
  }
}
