//
//  AppDelegate.swift
//  LoopbackFS
//
//  Created by Gunnar Herzog on 27/01/2017.
//  Copyright © 2017 KF Interactive GmbH. All rights reserved.
//

import Cocoa

@NSApplicationMain
class AppDelegate: NSObject, NSApplicationDelegate {

    @IBOutlet weak var window: NSWindow!

    private var notificationObservers: [NSObjectProtocol] = []
    private var rootPath: String!
    private lazy var loopFileSystem: LoopbackFS = {
        return LoopbackFS(rootPath: self.rootPath)
    }()

    private lazy var userFileSystem: GMUserFileSystem = {
        return GMUserFileSystem(delegate: self.loopFileSystem, isThreadSafe: false)
    }()


    func applicationDidFinishLaunching(_ aNotification: Notification) {
        let panel = NSOpenPanel()
        panel.canChooseFiles = false
        panel.canChooseDirectories = true
        panel.allowsMultipleSelection = false
        panel.directoryURL = URL(fileURLWithPath: "/tmp")
        let returnValue = panel.runModal()

        guard returnValue != NSFileHandlingPanelCancelButton, let rootPath = panel.urls.first?.path else { exit(0) }

        addNotifications()

        self.rootPath = rootPath

        var options: [String] = ["native_xattr", "volname=LoopbackFS"]

        if let volumeIconPath = Bundle.main.path(forResource: "LoopbackFS", ofType: "icns") {
            options.insert("volicon=\(volumeIconPath)", at: 0)
        }

        // Do not use the 'native_xattr' mount-time option unless the underlying
        // file system supports native extended attributes. Typically, the user
        // would be mounting an HFS+ directory through LoopbackFS, so we do want
        // this option in that case.
        userFileSystem.mount(atPath: "/Volumes/loop", withOptions: options)
    }

    func addNotifications() {
        let mountObserver = NotificationCenter.default.addObserver(forName: NSNotification.Name(kGMUserFileSystemDidMount), object: nil, queue: nil) { notification in
            print("Got didMount notification.")

            guard let userInfo = notification.userInfo, let mountPath = userInfo[kGMUserFileSystemMountPathKey] as? String else { return }

            let parentPath = (mountPath as NSString).deletingLastPathComponent
            NSWorkspace.shared().selectFile(mountPath, inFileViewerRootedAtPath: parentPath)
        }

        let failedObserver = NotificationCenter.default.addObserver(forName: NSNotification.Name(kGMUserFileSystemMountFailed), object: nil, queue: .main) { notification in
            print("Got mountFailed notification.")

            guard let userInfo = notification.userInfo, let error = userInfo[kGMUserFileSystemErrorKey] as? NSError else { return }

            print("kGMUserFileSystem Error: \(error), userInfo=\(error.userInfo)")
            let alert = NSAlert()
            alert.messageText = "Mount Failed"
            alert.informativeText = error.localizedDescription
            alert.runModal()

            NSApplication.shared().terminate(nil)
        }

        let unmountObserver = NotificationCenter.default.addObserver(forName: NSNotification.Name(kGMUserFileSystemDidUnmount), object: nil, queue: nil) { notification in
            print("Got didUnmount notification.")

            NSApplication.shared().terminate(nil)
        }

        self.notificationObservers = [mountObserver, failedObserver, unmountObserver]
    }

    func applicationShouldTerminate(_ sender: NSApplication) -> NSApplicationTerminateReply {
        notificationObservers.forEach {
            NotificationCenter.default.removeObserver($0)
        }
        notificationObservers.removeAll()

        userFileSystem.unmount()
        return .terminateNow
    }
}

