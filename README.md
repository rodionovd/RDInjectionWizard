### You can create everything. You can destroy everything as well. 💉

`RDInjectionWizard` is a small Objective-C framework for injecting our own code into another running applications. That is: you can create plugins for your favorite applications, modify system utilities (remember the [Dropbox's Finder hack](http://stackoverflow.com/questions/8380562/) to show icons over folders?) and make fun!  

To help you with this, `RDInjectionWizard` has a simple API for loading a prepared dynamic library into your target:  

```objc
pid_t target_proc = ...;
NSString *payload_path = @"/tmp/finder_payload.dylib"
RDInjectionWizard *wizard = [[RDInjectionWizard alloc] initWithTarget: target_proc
                                                              payload: payload_path];
[wizard injectUsingCompletionBlockWithSuccess: ^{
    NSLog(@"Yay, we've loaded some code into Finder!");
} failrue: ^(RDInjectionError error) {
    NSLog(@"Ooops, something went wrong. Error code: %d", error);
}];
```
> You can find way more examples in tests files within the `/RDInjectionWizardTests` directory of this repo.


#### What's about payload?
-----
Note that your payload is a dynamic library that have either a **constructor** function:  
```cpp
__attribute__((constructor))
void this_function_will_be_launched_when_the_library_is_injected_into_target_process(void)
{
    /* Do all the scary things here ... */
    fprintf(stderr, "Hello from payload!");
    draw_finder_folder_icons_and_more();
}
```
or an Objective-C **class implementing** the `+load` method:  

```objc
@implementation KWFinderModifer

+ (void)load
{
    NSLog(@"Hello from payload!");
    [self drawFinderIcons];
}

@end
```

> If your payload have both a constructor and a class with `+load` method, both will be executed at this point, so choose only one.
 
#### How to set it up  
------  
Pretty simple!  

1. First, compile your own version of `RDInjectionWizard` using the project in this repository, add this framework to your project (don't forget to create a Copy File build phase for it) and link your application against the `RDInjectionWizard`.

2. Next, open your application's Info.plist and add the following key to the dictionary there:  

  ```xml
  <key>SMPrivilegedExecutables</key>
  <dict>
    <key>me.rodionovd.RDInjectionWizard.injector</key>
    <string>certificate leaf[subject.CN]</string>
  </dict>
  ```  
You need this because `RDInjectionWizard` uses a privileged XPC helper tool internally (in order to perform any injection), and this tool (following the Apple's rules) must match in security requirements with its host application that appears to be your application. So you're just saying «Hey OS X, I do know about this helper, allow me to work with them".   

#### Interfaces  

> **Please note that these APIs is subject to change in future versions of the framework!**  

`RDInjectionWizard` exports only one self-titled class that have the following methods:  

-----
**`-initWithTarget:payload:`**  
Initialize an injection wizard for a given target and payload filepath.  
```objc
- (instancetype)initWithTarget: (pid_t)target payload: (NSString *)payload;
```  
**Parameters**   

Parameter   | Type | Description  
 :--------: | :-----------: | :----------  
 `target`   | `pid_t` | the identifer of the target process   
 `payload`  | `NSString *` | the full path to the payload library  
  
**Return value**  
An initialized injection wizard ready to be used in a wild.  

-----
**`-injectUsingCompletionBlockWithSuccess:failrue:`**  
Asynchronously inject the given target with the given payload library.

```objc
- (void)injectUsingCompletionBlockWithSuccess: (RDInjectionSuccededBlock) success
                                      failure: (RDInjectionFailedBlock) failure;
```

**Parameters**  

Parameter   | Type | Description  
 :--------: | :-----------: | :----------  
 `success`   | `RDInjectionSuccededBlock` | The block to be executed on the completion of the injection. It takes no arguments and has no return value    
 `failure`   | `RDInjectionFailedBlock` | The block to be executed upon an injection error. It has no return value and takes one argument: an error that occured during the injection  

-----
**`-sandboxFriendlyPayloadPath`**  
Looks up a sandbox-friendly location of the payload.  

```objc
- (NSString *)sandboxFriendlyPayloadPath;
```

**Return value**  
A new unique filename for the payload inside the target's sandbox container.

**Discussion**  
Use this method if you want to copy the payload into a location within the target's sandbox contaner that is said to be accessible for this process.



------
🔓
If you found any bug(s) or something, please open an issue or a pull request — I'd appreciate your help! `(^,,^)`

------

*Dmitry Rodionov, 2014*
*i.am.rodionovd@gmail.com*