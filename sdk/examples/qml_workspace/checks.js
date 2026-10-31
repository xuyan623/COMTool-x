// Pure UI packages can validate properties without any Python interpreter.
function validate(properties, context) {
    if(!properties.heading.trim())throw new Error("工作区标题不能为空")
    return properties
}
