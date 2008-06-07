# depends on: array.rb
#
# The dependency is from #alias_method.

##
# Objects of class Binding encapsulate the execution context at some
# particular place in the code and retain this context for future use. The
# variables, methods, value of self, and possibly an iterator block that can
# be accessed in this context are all retained. Binding objects can be created
# using Kernel#binding, and are made available to the callback of
# Kernel#set_trace_func.
#
# These binding objects can be passed as the second argument of the
# Kernel#eval method, establishing an environment for the evaluation.
#
#   class Demo
#     def initialize(n)
#       @secret = n
#     end
#     def getBinding
#       return binding()
#     end
#   end
#   
#   k1 = Demo.new(99)
#   b1 = k1.getBinding
#   k2 = Demo.new(-3)
#   b2 = k2.getBinding
#   
#   eval("@secret", b1)   #=> 99
#   eval("@secret", b2)   #=> -3
#   eval("@secret")       #=> nil
#
# Binding objects have no class-specific methods.

class Binding
  attr_accessor :context
  attr_accessor :caller_env

  def self.setup(ctx)
    bind = allocate()
    while ctx.kind_of? BlockContext and ctx.env.from_eval?
      ctx = ctx.env.home_block
    end
    
    bind.context = ctx
    return bind
  end

  def self.from_env(env)
    bind = allocate()

    bind.context = env.home_block
    bind.caller_env = env
    return bind
  end
end

module Kernel
  
  def local_variables
    ary = []
    ctx = MethodContext.current.sender
    
    while ctx.kind_of? BlockContext
      if names = ctx.method.local_names
        names.each { |n| ary << n.to_s }
      end
      ctx = ctx.home
    end
        
    if names = ctx.method.local_names
      names.each { |n| ary << n.to_s }
    end
    
    return ary
  end
  module_function :local_variables

  def binding
    Binding.setup MethodContext.current.sender
  end
  module_function :binding

  def eval(string, binding=nil, filename='(eval)', lineno=1)
    caller_env = nil
    if !binding
      context = MethodContext.current.sender
    elsif binding.kind_of? Proc
      binding = binding.binding
      context = binding.context
      caller_env = binding.caller_env
    elsif !binding.kind_of? Binding
      raise ArgumentError, "unknown type of binding"
    else
      context = binding.context
      caller_env = binding.caller_env
    end

    compiled_method = Compile.compile_string string, context, filename, lineno
    compiled_method.staticscope = context.method.staticscope.dup

    # This has to be setup so __FILE__ works in eval.
    script = CompiledMethod::Script.new
    script.path = filename
    compiled_method.staticscope.script = script

    be = BlockEnvironment.new
    be.from_eval!
    be.caller_env = caller_env # For correct 'caller' output
    be.under_context context, compiled_method
    be.call
  end
  module_function :eval
  private :eval

  ##
  # :call-seq:
  #   obj.instance_eval(string [, filename [, lineno]] )   => obj
  #   obj.instance_eval {| | block }                       => obj
  #
  # Evaluates a string containing Ruby source code, or the given block, within
  # the context of the receiver +obj+. In order to set the context, the
  # variable +self+ is set to +obj+ while the code is executing, giving the
  # code access to +obj+'s instance variables. In the version of
  # #instance_eval that takes a +String+, the optional second and third
  # parameters supply a filename and starting line number that are used when
  # reporting compilation errors.
  #
  #   class Klass
  #     def initialize
  #       @secret = 99
  #     end
  #   end
  #   k = Klass.new
  #   k.instance_eval { @secret }   #=> 99

  def instance_eval(string = nil, filename = "(eval)", line = 1, modeval = false, binding = nil, &prc)
    if prc
      if string
        raise ArgumentError, 'cannot pass both a block and a string to evaluate'
      end
      # Return a copy of the BlockEnvironment with the receiver set to self
      env = prc.block.redirect_to self
      env.method.staticscope = StaticScope.new(metaclass, env.method.staticscope)
      original_scope = prc.block.home.method.staticscope
      env.constant_scope = original_scope
      return env.call(*self)
    elsif string
      string = StringValue(string)

      if binding
        context = binding.context
      else
        context = MethodContext.current.sender
      end

      compiled_method = Compile.compile_string string, context, filename, line
      compiled_method.inherit_scope context.method

      # If this is a module_eval style evaluation, add self to the top of the
      # staticscope chain, so that methods and such are added directly to it.
      if modeval
        compiled_method.staticscope = StaticScope.new(self, compiled_method.staticscope)
      else

      # Otherwise add our metaclass, so thats where new methods go.
        compiled_method.staticscope = StaticScope.new(metaclass, compiled_method.staticscope)
      end

      # This has to be setup so __FILE__ works in eval.
      script = CompiledMethod::Script.new
      script.path = filename
      compiled_method.staticscope.script = script

      be = BlockEnvironment.new
      be.from_eval!
      be.under_context context, compiled_method
      be.call_on_instance(self)
    else
      raise ArgumentError, 'block not supplied'
    end
  end

end

class Module

  #--
  # These have to be aliases, not methods that call instance eval, because we
  # need to pull in the binding of the person that calls them, not the
  # intermediate binding.
  #++

  def module_eval(string = Undefined, filename = "(eval)", line = 1, &prc)
    # we have a custom version with the prc, rather than using instance_exec
    # so that we can setup the StaticScope properly.
    if prc
      unless string.equal?(Undefined)
        raise ArgumentError, "cannot pass both string and proc"
      end

      env = prc.block.redirect_to self
      env.method.staticscope = StaticScope.new(self, env.method.staticscope)
      return env.call()
    elsif string.equal?(Undefined)
      raise ArgumentError, 'block not supplied'
    end

    context = MethodContext.current.sender

    string = StringValue(string)

    compiled_method = Compile.compile_string string, context, filename, line

    # The staticscope of a module_eval CM is the receiver of module_eval
    ss = StaticScope.new(self, context.method.staticscope)

    # This has to be setup so __FILE__ works in eval.
    script = CompiledMethod::Script.new
    script.path = filename
    ss.script = script

    compiled_method.staticscope = ss

    # The gist of this code is that we need the receiver's static scope
    # but the caller's binding to implement the proper constant behavior
    be = BlockEnvironment.new
    be.from_eval!
    be.under_context context, compiled_method
    be.make_independent
    be.home.receiver = self
    be.home.make_independent
    # open_module and friends in the VM use this field to determine scope
    be.home.method.staticscope = ss
    be.call
  end
  alias_method :class_eval, :module_eval

  def _eval_under(*args, &block)
    raise "not yet" unless block

    env = block.block.redirect_to self
    env.method.staticscope = StaticScope.new(self, env.method.staticscope)

    return env.call(*args)
  end
  private :_eval_under
end
