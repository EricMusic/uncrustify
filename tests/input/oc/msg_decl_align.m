// this shouldn't be touched as selector is on one line
- (NSDragOperation)tableView:(NSTableView*)tv validateDrop:(id <NSDraggingInfo>)info proposedRow:(NSInteger)row proposedDropOperation:(NSTableViewDropOperation)operation
{
   if (operation == NSTableViewDropAbove)
      return NSDragOperationNone;
   
   NSPasteboard *pboard = [info draggingPasteboard];
   if ([pboard dataForType:@"PBGitRef"])
      return NSDragOperationMove;
   
   return NSDragOperationNone;
}

// this should be aligned on colons
- (NSDragOperation)tableView:(NSTableView*)tv
validateDrop:(id <NSDraggingInfo>)info
proposedRow:(NSInteger)row
proposedDropOperation:(NSTableViewDropOperation)operation
{
   if (operation == NSTableViewDropAbove)
      return NSDragOperationNone;
   
   NSPasteboard *pboard = [info draggingPasteboard];
   if ([pboard dataForType:@"PBGitRef"])
      return NSDragOperationMove;
   
   return NSDragOperationNone;
}

