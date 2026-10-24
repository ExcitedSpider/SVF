; A constant pointer has no instruction definition with an AE state.
source_filename = "null-base.c"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

define i32 @main() {
entry:
  %p = getelementptr inbounds i8, ptr null, i64 0
  %v = load i8, ptr %p
  %result = sext i8 %v to i32
  ret i32 %result
}
