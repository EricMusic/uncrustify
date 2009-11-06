// test: single lines should be left as they are
// [self findstart:&startBarcode end:&endBarcode forLine:greenScalePixels derivative:greenDerivative centerAt:xAxisCenterPoint min:&minValue max:&maxValue];

// test: fix colon alignment which is broken by editor on paste
// e.g. some editors will align line starts and not the colons
[self findstart:&startBarcode
            end:&endBarcode
        forLine:greenScalePixels
     derivative:greenDerivative
       centerAt:xAxisCenterPoint
            min:&minValue
            max:&maxValue];

// test: some tricky msg send with lots of stuff to throw the logic off
// like nested msgs, selector notation, a commented line and some negative alignment.
[[self class] findstart:&startBarcode
                    end:&endBarcode
                forLine:greenScalePixels
             derivative:[greenDerivative msg:[super msg]]
               centerAt:[xAxisCenterPoint msg:arg] revolveAround:[yAxisCenterPoint intValue]
/*                    min:&minValue */
                     max:&maxValue
                sel:@selector(blaMsg:)];

// test: and now do the same inside a method to test if other indentation
// will be done correctly
- (NSString *) someMethodWithArg:(BOOL)arg {

    return [[self class] findstart:&startBarcode
                               end:&endBarcode
                           forLine:greenScalePixels
                        derivative:[greenDerivative msg:[super msg]]
                          centerAt:[xAxisCenterPoint msg:arg] revolveAround:[yAxisCenterPoint intValue]
                        // next line should not be aligned at all because test cfg file has a span of 1 and this line has no oc colon
                    sel:@selector(blaMsg:)];
}