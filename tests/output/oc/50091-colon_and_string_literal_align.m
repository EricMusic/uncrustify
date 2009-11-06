# pragma mark Add ref methods
- (void) addRef:(id)sender {
    [errorMessage setStringValue:@""];
    [NSApp beginSheet:newBranchSheet
       modalForWindow:[[historyController view] window]
        modalDelegate:NULL
       didEndSelector:NULL
          contextInfo:NULL];
}

- (void) pushButton:(id)sender {
    [sender setEnabled:NO];
    NSString * refName = [[[[historyController repository] currentBranch] simpleRef] refForSpec];
    if (refName) {
        [self pushImpl:refName];
    } else {
        [[NSAlert alertWithMessageText:messageText
                         defaultButton:nil
                       alternateButton:nil
                           otherButton:nil
             informativeTextWithFormat:infoText] beginSheetModalForWindow:[self window] modalDelegate:self didEndSelector:nil contextInfo:nil];


        [[NSAlert alertWithMessageText:[NSString stringWithFormat:@"Push"]
                         defaultButton:@"OK"
                       alternateButton:@"Cancel"
                           otherButton:nil
             informativeTextWithFormat:@"Remote name is nil.\n\n"
                                       @"Please select a local branch which has a default remote reference from \n"
                                       @"the branch dropdown menu"
                                       @"some other text"] runModal];
        return;
    }

    [sender setEnabled:YES];
    NSLog([NSString stringWithFormat:@"Push hit for %@!", refName]);
} /* pushButton */


init(){
    [NSException raise:NSInternalInconsistency
                format:@"An internal inconsistency was raised"];
}
