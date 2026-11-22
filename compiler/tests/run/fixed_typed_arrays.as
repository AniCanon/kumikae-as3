package {
    public class fixed_typed_arrays {
        public function fixed_typed_arrays() {
            var size:int = 3;
            var nums:[]int = new [size] int;
            nums[1] = 19;
            trace(nums.length, nums[0], nums[1]);
            var alias:* = nums;
            alias[0] = 7;
            try { alias.fixed = false; } catch (e:Error) { trace("fixed"); }
            try { alias.length = 0; } catch (e:Error) { trace("length"); }
            try { alias.push(8); } catch (e:Error) { trace("push"); }
            try { nums[size] = 1; } catch (e:RangeError) { trace("typed bounds"); }
            try { alias[size] = 1; } catch (e:RangeError) { trace("dynamic bounds"); }
            trace(nums.length, nums[0], alias is Vector.<int>);
            var ordinary:Vector.<int> = new Vector.<int>(1, true);
            alias = ordinary;
            alias.fixed = false;
            trace(ordinary.push(8));
            var nested:[][]int = new [][]int { nums, new []int { 3, 4 } };
            trace(nested[0][1], nested[1][0], nested[1].length);
            try { var wrong:[]int = alias; } catch (e:TypeError) { trace("nominal"); }
        }
    }
}
