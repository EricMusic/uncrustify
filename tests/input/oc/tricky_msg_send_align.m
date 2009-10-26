- (void) bind:(NSString *)binding toObject:(id)observable withKeyPath:(NSString *)keyPath options:(NSDictionary *)options {
    
    // with an "align_oc_msg_colon_span" setting of 1 the following msg send should avoid alignment by its colons.
    NSDictionary *info = [NSDictionary dictionaryWithObjectsAndKeys:
                              [self fileURL].path, @"GIT_DIR",
                              [[self fileURL].path stringByAppendingPathComponent:@"index"], @"GIT_INDEX_FILE",
                              nil
                          ];
    
    // yep, this is one of the longest method signatures in Cocoa... here with an "align_oc_msg_colon_span" setting of 1 
    // the method below should be aligned on its colons full on through for each line since the colons appear contigously.
    NSImageRep * rep = [[[NSImageRep alloc] init] drawInRect:dstSpacePortionRect 
                                                  fromRect:srcSpacePortionRect 
                                                  operation:op 
                                                  fraction:requestedAlpha 
                                                  respectFlipped:respectContextIsFlipped 
                                                  hints:hints
                        ];
    
    // also the closing squares (incl. semicolon) should keep their original column, with indentation of the the middle part
    // preserved.
}
