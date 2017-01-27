//
//  NSError+POSIX.swift
//  LoopbackFS
//
//  Created by Gunnar Herzog on 27/01/2017.
//  Copyright © 2017 KF Interactive GmbH. All rights reserved.
//

import Foundation

extension NSError {
    convenience init(posixErrorCode err: Int32) {
        self.init(domain: NSPOSIXErrorDomain, code: Int(err), userInfo: [NSLocalizedDescriptionKey: String(cString: strerror(err))])
    }
}
