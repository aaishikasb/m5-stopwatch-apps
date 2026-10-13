import Foundation
import Speech

func output(_ value: [String: Any], code: Int32 = 0) -> Never {
    let data = try! JSONSerialization.data(withJSONObject: value, options: [.sortedKeys])
    FileHandle.standardOutput.write(data)
    FileHandle.standardOutput.write(Data("\n".utf8))
    exit(code)
}

let recognizer = SFSpeechRecognizer(locale: Locale(identifier: "en-US"))
let arguments = CommandLine.arguments
if arguments.count == 2 && arguments[1] == "--check" {
    output(["authorization": SFSpeechRecognizer.authorizationStatus().rawValue,
            "onDevice": recognizer?.supportsOnDeviceRecognition ?? false,
            "available": recognizer?.isAvailable ?? false])
}
if arguments.count == 2 && arguments[1] == "--authorize" {
    SFSpeechRecognizer.requestAuthorization { status in
        output(["authorized": status == .authorized], code: status == .authorized ? 0 : 1)
    }
    RunLoop.main.run()
}
guard arguments.count == 2 else {
    output(["error": "usage", "message": "Pass a WAV path, --check, or --authorize."], code: 1)
}
guard SFSpeechRecognizer.authorizationStatus() == .authorized else {
    output(["error": "permission", "message": "Allow Speech Recognition for Pulse Speech in macOS settings."], code: 1)
}
guard let recognizer, recognizer.supportsOnDeviceRecognition else {
    output(["error": "model", "message": "On-device English speech recognition is not available on this Mac."], code: 1)
}
guard FileManager.default.fileExists(atPath: arguments[1]) else {
    output(["error": "file", "message": "Audio file is missing."], code: 1)
}
let request = SFSpeechURLRecognitionRequest(url: URL(fileURLWithPath: arguments[1]))
request.requiresOnDeviceRecognition = true
request.shouldReportPartialResults = false
request.taskHint = .dictation
var finished = false
let task = recognizer.recognitionTask(with: request) { result, error in
    if let result, result.isFinal {
        finished = true
        let text = result.bestTranscription.formattedString.trimmingCharacters(in: .whitespacesAndNewlines)
        if text.isEmpty { output(["error": "empty", "message": "No speech recognized."], code: 1) }
        output(["text": text])
    }
    if let error {
        finished = true
        let nsError = error as NSError
        output(["error": "recognition", "message": nsError.localizedDescription], code: 1)
    }
}
let deadline = Date().addingTimeInterval(45)
while !finished && Date() < deadline {
    RunLoop.main.run(until: Date().addingTimeInterval(0.05))
}
task.cancel()
output(["error": "timeout", "message": "Speech recognition timed out."], code: 1)
